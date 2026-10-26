;;; uvm_completion.el --- helpers for uvm_completion.sh  -*- lexical-binding: t; -*-
;;
;; Loaded into the test daemon by tests/integration/uvm_corpus/uvm_completion.sh.
;; The fixture is copied into its own directory (outside the repo's git
;; root) and added as a workspace folder, so lsp-mode starts a separate
;; svlsp process for it: the UVM library DB it attaches never leaks into
;; the shared server the rest of the suite uses, and every timing below
;; is for a fresh server.

(require 'cl-lib)
(require 'lsp-mode)

(defvar svlsp-uvm/buf nil "The fixture buffer.")
(defvar svlsp-uvm/perf-log nil "File each request's timing is appended to.")
(defvar svlsp-uvm/server-log nil
  "The fixture server's `--log-requests' file (its own time per request).")

(defun svlsp-uvm/last-server-line (what)
  "The server's \"wait W ms, run R ms\" for its latest WHAT line, or \"\".
The server writes the line before it responds, so after a request returns
its line is the latest one."
  (if (not (and svlsp-uvm/server-log (file-exists-p svlsp-uvm/server-log)))
      ""
    (with-temp-buffer
      (insert-file-contents svlsp-uvm/server-log)
      (goto-char (point-max))
      (if (re-search-backward
           (concat "^\\[request\\] " (regexp-quote what)
                   " .*wait \\([0-9.]+\\) ms, run \\([0-9.]+\\) ms$")
           nil t)
          (format "server %9.1f ms (wait %.1f)"
                  (string-to-number (match-string 2))
                  (string-to-number (match-string 1)))
        ""))))

(defun svlsp-uvm/log (fmt &rest args)
  (when svlsp-uvm/perf-log
    (write-region (concat (apply #'format fmt args) "\n") nil svlsp-uvm/perf-log 'append 'silent)))

(defun svlsp-uvm/position-after (probe)
  "LSP position right after PROBE's text in the fixture buffer."
  (with-current-buffer svlsp-uvm/buf
    (save-excursion
      (goto-char (point-min))
      (unless (search-forward probe nil t)
        (error "probe not found: %s" probe))
      (list :line (1- (line-number-at-pos)) :character (current-column)))))

(defun svlsp-uvm/labels (probe)
  "Completion labels right after PROBE, timing the request into the log."
  (let* ((pos (svlsp-uvm/position-after probe))
         (lsp-response-timeout 300)
         (t0 (float-time))
         (result (with-current-buffer svlsp-uvm/buf
                   (lsp-request "textDocument/completion"
                                (list :textDocument (list :uri (lsp--buffer-uri))
                                      :position pos))))
         (ms (* 1000 (- (float-time) t0)))
         (items (cond ((hash-table-p result) (gethash "items" result))
                      ((listp result) result)
                      ((vectorp result) (append result nil))))
         (labels (mapcar (lambda (i) (gethash "label" i)) items)))
    (svlsp-uvm/log "  %-34s %9.1f ms %6d items   %s" probe ms (length labels)
                   (svlsp-uvm/last-server-line "textDocument/completion"))
    labels))

(defun svlsp-uvm/open (dir timeout)
  "Open DIR/uvm_tb.sv in its own workspace; wait until the library is loaded.
Returns t, or a string saying what didn't happen within TIMEOUT seconds."
  (lsp-workspace-folders-add dir)
  (setq svlsp-uvm/server-log (expand-file-name "requests.log" dir))
  (let ((t0 (float-time)))
    (setq svlsp-uvm/buf (find-file-noselect (expand-file-name "uvm_tb.sv" dir)))
    ;; Only this workspace's server is started here, so the arguments
    ;; apply to it alone.
    (let ((svlsp-test-server-args (list "--log-requests" svlsp-uvm/server-log)))
      (with-current-buffer svlsp-uvm/buf
        (normal-mode)
        (lsp)))
    (if (not (with-current-buffer svlsp-uvm/buf (svlsp-test/wait-for-lsp timeout)))
        "no initialized workspace"
      (svlsp-uvm/log "  %-34s %9.1f ms" "initialize" (* 1000 (- (float-time) t0)))
      ;; The project (and its library DB) loads on the first request for
      ;; the file: poll until a library class is offered.
      (let ((deadline (+ (float-time) timeout)) found)
        (while (and (not found) (< (float-time) deadline))
          (setq found (member "uvm_component" (svlsp-uvm/labels "probe-lib: uvm_comp")))
          (unless found (sit-for 1)))
        (svlsp-uvm/log "  %-34s %9.1f ms   %s" "library visible after"
                       (* 1000 (- (float-time) t0))
                       (replace-regexp-in-string
                        "^server" "first compile" (svlsp-uvm/last-server-line "compile")))
        (if found t "uvm_component never offered at probe-lib")))))

(defun svlsp-uvm/check (probe want &optional dont-want)
  "t if completion after PROBE offers every label in WANT and none in DONT-WANT,
else a string naming what was missing or unexpected."
  (condition-case err
      (let* ((labels (svlsp-uvm/labels probe))
             (missing (cl-remove-if (lambda (w) (member w labels)) want))
             (extra (cl-remove-if-not (lambda (w) (member w labels)) dont-want)))
        (if (and (null missing) (null extra))
            t
          (format "%d items; missing %S; unexpected %S" (length labels) missing extra)))
    (error (format "elisp-error: %s" (error-message-string err)))))

(defun svlsp-uvm/check-definition (anchor want-file)
  "t if definition on the last character of ANCHOR's text lands in WANT-FILE,
an absolute path that exists; else a string naming where it went."
  (condition-case err
      (let* ((pos (svlsp-uvm/position-after anchor))
             (pos (plist-put pos :character (1- (plist-get pos :character))))
             (lsp-response-timeout 300)
             (result (with-current-buffer svlsp-uvm/buf
                       (lsp-request "textDocument/definition"
                                    (list :textDocument (list :uri (lsp--buffer-uri))
                                          :position pos))))
             (loc (cond ((hash-table-p result) result)
                        ((vectorp result) (and (> (length result) 0) (aref result 0)))
                        ((listp result) (car result))))
             (file (and loc (lsp--uri-to-path (gethash "uri" loc)))))
        (svlsp-uvm/log "  %-34s %s" (concat "definition: " anchor) (or file "nothing"))
        (cond ((null file) "no definition")
              ((not (file-exists-p file)) (format "%s does not exist" file))
              ((not (string= (file-truename file) (file-truename want-file)))
               (format "went to %s, not %s" file want-file))
              (t t)))
    (error (format "elisp-error: %s" (error-message-string err)))))

(defun svlsp-uvm/check-references (anchor want-file want-line)
  "t if references on the last character of ANCHOR's text include WANT-FILE
at 0-based WANT-LINE (a library declaration); else a string saying what came
back."
  (condition-case err
      (let* ((pos (svlsp-uvm/position-after anchor))
             (pos (plist-put pos :character (1- (plist-get pos :character))))
             (lsp-response-timeout 300)
             (t0 (float-time))
             (result (with-current-buffer svlsp-uvm/buf
                       (lsp-request "textDocument/references"
                                    (list :textDocument (list :uri (lsp--buffer-uri))
                                          :position pos
                                          :context (list :includeDeclaration t)))))
             (ms (* 1000 (- (float-time) t0)))
             (locs (append result nil))
             (want (file-truename want-file))
             (library (cl-count-if
                       (lambda (l) (string-prefix-p (file-name-directory
                                                     (directory-file-name
                                                      (file-name-directory want)))
                                                    (file-truename (lsp--uri-to-path
                                                                    (gethash "uri" l)))))
                       locs))
             (found (cl-some
                     (lambda (l)
                       (and (string= (file-truename (lsp--uri-to-path (gethash "uri" l))) want)
                            (= (gethash "line" (gethash "start" (gethash "range" l)))
                               want-line)))
                     locs)))
        (svlsp-uvm/log "  %-34s %9.1f ms %6d locations (%d in the library)   %s"
                       (concat "references: " anchor) ms (length locs) library
                       (svlsp-uvm/last-server-line "textDocument/references"))
        (if found t
          (format "%d locations, %d in the library, none at %s:%d"
                  (length locs) library want-file want-line)))
    (error (format "elisp-error: %s" (error-message-string err)))))

(defun svlsp-uvm/close (dir)
  (when (buffer-live-p svlsp-uvm/buf)
    (with-current-buffer svlsp-uvm/buf (set-buffer-modified-p nil))
    (kill-buffer svlsp-uvm/buf))
  ;; Shuts down the fixture's own server and forgets the folder.
  (ignore-errors (lsp-workspace-folders-remove dir))
  t)

;;; uvm_completion.el ends here
