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
    (svlsp-uvm/log "  %-34s %9.1f ms %6d items" probe ms (length labels))
    labels))

(defun svlsp-uvm/open (dir timeout)
  "Open DIR/uvm_tb.sv in its own workspace; wait until the library is loaded.
Returns t, or a string saying what didn't happen within TIMEOUT seconds."
  (lsp-workspace-folders-add dir)
  (let ((t0 (float-time)))
    (setq svlsp-uvm/buf (find-file-noselect (expand-file-name "uvm_tb.sv" dir)))
    (with-current-buffer svlsp-uvm/buf
      (normal-mode)
      (lsp))
    (if (not (with-current-buffer svlsp-uvm/buf (svlsp-test/wait-for-lsp timeout)))
        "no initialized workspace"
      (svlsp-uvm/log "  %-34s %9.1f ms" "initialize" (* 1000 (- (float-time) t0)))
      ;; The project (and its library DB) loads on the first request for
      ;; the file: poll until a library class is offered.
      (let ((deadline (+ (float-time) timeout)) found)
        (while (and (not found) (< (float-time) deadline))
          (setq found (member "uvm_component" (svlsp-uvm/labels "probe-lib: uvm_comp")))
          (unless found (sit-for 1)))
        (svlsp-uvm/log "  %-34s %9.1f ms" "library visible after" (* 1000 (- (float-time) t0)))
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

(defun svlsp-uvm/close (dir)
  (when (buffer-live-p svlsp-uvm/buf)
    (with-current-buffer svlsp-uvm/buf (set-buffer-modified-p nil))
    (kill-buffer svlsp-uvm/buf))
  ;; Shuts down the fixture's own server and forgets the folder.
  (ignore-errors (lsp-workspace-folders-remove dir))
  t)

;;; uvm_completion.el ends here
