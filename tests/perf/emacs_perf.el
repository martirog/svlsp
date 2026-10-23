;;; emacs_perf.el --- latency benchmark through lsp-mode  -*- lexical-binding: t; -*-
;;
;; Loaded into the test daemon by tests/perf/emacs_perf.sh. Runs the same
;; scenario (tests/perf/<name>.json) and workspace as tools/lsp_perf.py,
;; but through lsp-mode, so each time includes what the editor adds: the
;; JSON parse into Lisp objects, and for the rows marked [capf] / [render]
;; lsp-mode's own completion pipeline (sorting, prefix guessing,
;; filtering) or hover rendering. Raw samples go to a JSON file that
;; `tools/lsp_perf.py --report' summarizes, so both harnesses print the
;; same table.

(require 'cl-lib)
(require 'json)
(require 'lsp-mode)
(require 'lsp-completion)

(defvar svlsp-perf/buf nil "The scenario's open buffer.")
(defvar svlsp-perf/log nil "The server's `--log-requests' file.")
(defvar svlsp-perf/log-pos 0 "How much of `svlsp-perf/log' has been read.")
(defvar svlsp-perf/diag-time nil "When diagnostics last arrived.")
(defvar svlsp-perf/pid nil "The scenario server's process id.")
(defvar svlsp-perf/memory nil "Memory rows (plists), latest first.")
(defvar svlsp-perf/exact-peak nil
  "Non-nil if each phase's peak is exact (VmHWM reset at the phase start).")

;;; Helpers -------------------------------------------------------------

(defun svlsp-perf/ms-since (t0) (* 1000 (- (float-time) t0)))

(defun svlsp-perf/log-lines ()
  "The server log's new (WHAT WAIT RUN) entries since the last call."
  (let ((size (or (file-attribute-size (file-attributes svlsp-perf/log)) 0))
        lines)
    (when (> size svlsp-perf/log-pos)
      (with-temp-buffer
        (insert-file-contents svlsp-perf/log nil svlsp-perf/log-pos size)
        (setq svlsp-perf/log-pos size)
        (goto-char (point-min))
        (while (re-search-forward
                "^\\[request\\] \\([^ \n]+\\).* wait \\([0-9.]+\\) ms, run \\([0-9.]+\\) ms$" nil t)
          (push (list (match-string 1) (string-to-number (match-string 2))
                      (string-to-number (match-string 3)))
                lines))))
    (nreverse lines)))

(defun svlsp-perf/server-time (method)
  "(WAIT . RUN) of the latest new log line for METHOD, or (nil . nil)."
  (let ((hit (cons nil nil)))
    (dolist (l (svlsp-perf/log-lines) hit)
      (when (equal (car l) method)
        (setq hit (cons (nth 1 l) (nth 2 l)))))))

(defun svlsp-perf/point-at (at)
  "Buffer position for an `at' spec (see tests/perf/README.md)."
  (with-current-buffer svlsp-perf/buf
    (save-excursion
      (goto-char (point-min))
      (let* ((find (plist-get at :find))
             (sub (plist-get at :sub))
             (off (or (plist-get at :offset) 1))
             (start (progn (unless (search-forward find nil t)
                             (error "text not found: %s" find))
                           (match-beginning 0)))
             (base (if sub (+ start (string-match (regexp-quote sub) find)) start))
             (len (length (or sub find))))
        (+ base (if (equal off "end") len off))))))

(defun svlsp-perf/lsp-position (pt)
  (with-current-buffer svlsp-perf/buf
    (save-excursion (goto-char pt) (lsp--cur-position))))

(defun svlsp-perf/count (method result)
  (cond ((null result) 0)
        ((equal method "textDocument/completion")
         (length (if (hash-table-p result) (gethash "items" result) result)))
        ((equal method "textDocument/rename")
         (let ((n 0))
           (when-let ((changes (gethash "changes" result)))
             (maphash (lambda (_ edits) (cl-incf n (length edits))) changes))
           n))
        ((equal method "textDocument/signatureHelp")
         (length (gethash "signatures" result)))
        ((sequencep result) (length result))
        (t 1)))

(defun svlsp-perf/params (req)
  "REQ's LSP params: its own, plus the document and position."
  (let ((params (copy-sequence (plist-get req :params)))
        (at (plist-get req :at)))
    (when (string-prefix-p "textDocument/" (plist-get req :method))
      (setq params (plist-put params :textDocument
                              (with-current-buffer svlsp-perf/buf
                                (lsp--text-document-identifier)))))
    (when at
      (setq params (plist-put params :position
                              (svlsp-perf/lsp-position (svlsp-perf/point-at at)))))
    params))

(defun svlsp-perf/request (method params)
  (with-current-buffer svlsp-perf/buf
    (let ((lsp-response-timeout 600))
      (lsp-request method params))))

(defun svlsp-perf/row (name tier client run wait results)
  (list :name name :tier tier :client (vconcat client)
        :server_run (vconcat (delq nil run)) :server_wait (vconcat (delq nil wait))
        :results results))

(defun svlsp-perf/measure (name tier repeat warmup thunk &optional method)
  "Time THUNK (returns a result count) REPEAT times after WARMUP; a row.
METHOD names the server log line to pair with each run."
  (let (client run wait results)
    (dotimes (i (+ warmup repeat))
      (svlsp-perf/log-lines)
      (let* ((t0 (float-time))
             (n (funcall thunk))
             (ms (svlsp-perf/ms-since t0))
             (srv (if method (svlsp-perf/server-time method) (cons nil nil))))
        (setq results n)
        (when (>= i warmup)
          (push ms client) (push (cdr srv) run) (push (car srv) wait))))
    (svlsp-perf/row name tier (nreverse client) (nreverse run) (nreverse wait) results)))

(defun svlsp-perf/capf-count (pt)
  "Run lsp-mode's completion-at-point at PT, uncached; the candidate count."
  (with-current-buffer svlsp-perf/buf
    (save-excursion
      (goto-char pt)
      (lsp-completion--clear-cache)
      (let ((capf (lsp-completion-at-point)))
        (length (all-completions "" (nth 2 capf)))))))

(defun svlsp-perf/wait-diagnostics (t0 timeout)
  "Wait until diagnostics arrive after T0; ms from T0, or nil."
  (let ((deadline (+ (float-time) timeout)))
    (while (and (not (and svlsp-perf/diag-time (> svlsp-perf/diag-time t0)))
                (< (float-time) deadline))
      (sit-for 0.01))
    (and svlsp-perf/diag-time (> svlsp-perf/diag-time t0)
         (* 1000 (- svlsp-perf/diag-time t0)))))

(defun svlsp-perf/on-diagnostics ()
  (setq svlsp-perf/diag-time (float-time)))

;;; Memory --------------------------------------------------------------
;; The same per-phase readings as tools/lsp_perf.py's `Memory': the RSS
;; at a phase's end, its anonymous part and the peak during it (VmHWM,
;; reset through clear_refs at each phase start; without that reset, the
;; peak since the server started -- no sampler here, a timer would
;; perturb the timings).

(defun svlsp-perf/mem-status ()
  "Alist (FIELD . kB) of the server's Vm*/Rss* status lines, or nil."
  (ignore-errors
    (with-temp-buffer
      (insert-file-contents (format "/proc/%d/status" svlsp-perf/pid))
      (let (out)
        (while (re-search-forward "^\\(\\(?:Vm\\|Rss\\)[A-Za-z]+\\):\\s-+\\([0-9]+\\)" nil t)
          (push (cons (match-string 1) (string-to-number (match-string 2))) out))
        out))))

(defun svlsp-perf/reset-peak ()
  (condition-case nil
      (progn (write-region "5" nil (format "/proc/%d/clear_refs" svlsp-perf/pid) nil 'silent)
             t)
    (error nil)))

(defun svlsp-perf/mem-start (pid)
  (setq svlsp-perf/pid pid
        svlsp-perf/memory nil
        svlsp-perf/exact-peak (svlsp-perf/reset-peak)))

(defun svlsp-perf/mem-phase (name)
  "Close the memory phase that started at the previous call as NAME."
  (when-let ((s (and svlsp-perf/pid (svlsp-perf/mem-status))))
    (push (list :name name :rss_kb (cdr (assoc "VmRSS" s))
                :anon_kb (or (cdr (assoc "RssAnon" s)) :null)
                :peak_kb (cdr (assoc "VmHWM" s)))
          svlsp-perf/memory)
    (when svlsp-perf/exact-peak (svlsp-perf/reset-peak))))

(defun svlsp-perf/mem-summary ()
  (let ((rows (reverse svlsp-perf/memory)))
    (list :exact_peak (if svlsp-perf/exact-peak t :false)
          :phases (vconcat rows)
          :peak_kb (apply #'max 0 (mapcar (lambda (r) (plist-get r :peak_kb)) rows)))))

;;; The run ---------------------------------------------------------------

(defun svlsp-perf/open (dir file ready timeout)
  "Open DIR/FILE in its own workspace; wait for the server (and READY)."
  (lsp-workspace-folders-add dir)
  (setq svlsp-perf/log (expand-file-name "requests.log" dir)
        svlsp-perf/log-pos 0)
  (let ((t0 (float-time)) phases)
    (setq svlsp-perf/buf (find-file-noselect (expand-file-name file dir)))
    (let ((svlsp-test-server-args (list "--log-requests" svlsp-perf/log)))
      (with-current-buffer svlsp-perf/buf
        (normal-mode)
        (lsp)))
    (unless (with-current-buffer svlsp-perf/buf (svlsp-test/wait-for-lsp timeout))
      (error "no initialized workspace"))
    (push (cons "initialize" (svlsp-perf/ms-since t0)) phases)
    (svlsp-perf/mem-start
     (with-current-buffer svlsp-perf/buf
       (process-id (lsp--workspace-cmd-proc (car (lsp-workspaces))))))
    (svlsp-perf/mem-phase "initialize + open")
    (when ready
      (let ((pos (svlsp-perf/lsp-position (svlsp-perf/point-at (plist-get ready :at))))
            (deadline (+ (float-time) timeout)) found)
        (while (not found)
          (let* ((res (svlsp-perf/request
                       "textDocument/completion"
                       (list :textDocument (with-current-buffer svlsp-perf/buf
                                             (lsp--text-document-identifier))
                             :position pos)))
                 (items (if (hash-table-p res) (gethash "items" res) res)))
            (setq found (seq-find (lambda (i) (equal (gethash "label" i)
                                                     (plist-get ready :label)))
                                  items)))
          (unless found
            (when (> (float-time) deadline) (error "library never became visible"))
            (sit-for 0.5)))
        (push (cons "open -> library visible" (svlsp-perf/ms-since t0)) phases)
        (svlsp-perf/mem-phase "library load")))
    (nreverse phases)))

(defun svlsp-perf/requests (scenario repeat warmup only)
  (let (rows)
    (seq-doseq (req (plist-get scenario :requests))
      (let* ((name (plist-get req :name))
             (method (plist-get req :method))
             (tier (plist-get req :tier)))
        (when (or (null only) (string-match-p only name))
          (let ((params
                 (if-let ((from (plist-get req :resolveFrom)))
                     (let* ((res (svlsp-perf/request
                                  "textDocument/completion"
                                  (svlsp-perf/params (list :method "textDocument/completion"
                                                           :at (plist-get from :at)))))
                            (items (if (hash-table-p res) (gethash "items" res) res)))
                       (or (seq-find (lambda (i) (equal (gethash "label" i)
                                                        (plist-get from :label)))
                                     items)
                           (error "%s not offered" (plist-get from :label))))
                   (svlsp-perf/params req))))
            (push (svlsp-perf/measure
                   name tier repeat warmup
                   (lambda () (svlsp-perf/count method (svlsp-perf/request method params)))
                   method)
                  rows)
            ;; What the user waits for: lsp-mode's completion pipeline, and
            ;; a rendered hover.
            (cond
             ((equal method "textDocument/completion")
              (let ((pt (svlsp-perf/point-at (plist-get req :at))))
                (push (svlsp-perf/measure
                       (concat name " [capf]") tier repeat warmup
                       (lambda () (svlsp-perf/capf-count pt))
                       method)
                      rows)))
             ((equal method "textDocument/hover")
              (push (svlsp-perf/measure
                     (concat name " [render]") tier repeat warmup
                     (lambda ()
                       (let ((res (svlsp-perf/request method params)))
                         (if (and res (gethash "contents" res))
                             (progn (lsp--render-on-hover-content (gethash "contents" res) t) 1)
                           0)))
                     method)
                    rows)))))))
    (nreverse rows)))

(defun svlsp-perf/keystroke-idle ()
  "Make the idle timers an edit just armed count from now, as after a keystroke.
An edit made from emacsclient happens while Emacs has long been idle, so
an idle timer it arms (lsp-mode's delayed full-sync didChange, 1.0 s, and
its `lsp--on-idle', 0.5 s, which rebuilds the breadcrumb with a
documentSymbol request and so flushes the change) would fire at once. A
real keystroke resets idleness: each fires its delay after it. So every
pending one-shot idle timer (only ones this edit armed: older ones have
fired) becomes a plain timer with the same delay, in place, so code that
cancels it still can."
  (dolist (tm (copy-sequence timer-idle-list))
    (unless (timer--repeat-delay tm)
      (let ((delay (float-time (timer--time tm))))
        (cancel-timer tm)
        (setf (timer--idle-delay tm) nil)
        (timer-set-time tm (timer-relative-time nil delay))
        (timer-activate tm)))))

(defun svlsp-perf/edit (edit repeat warmup)
  "Keystroke -> diagnostics, through lsp-mode's didChange debounce."
  (let ((pt (svlsp-perf/point-at (plist-get edit :at)))
        (text (plist-get edit :text))
        samples)
    (dotimes (i (+ warmup repeat))
      (with-current-buffer svlsp-perf/buf
        (let ((t0 (float-time)))
          (save-excursion
            (goto-char pt)
            (if (cl-evenp i) (insert text) (delete-char (length text))))
          (svlsp-perf/keystroke-idle)
          (let ((ms (svlsp-perf/wait-diagnostics t0 60)))
            (unless ms (error "no diagnostics after edit %d" i))
            (when (>= i warmup) (push ms samples))))))
    (when (cl-oddp (+ warmup repeat))
      (with-current-buffer svlsp-perf/buf
        (let ((t0 (float-time)))
          (save-excursion (goto-char pt) (delete-char (length text)))
          (svlsp-perf/wait-diagnostics t0 60))))
    (let ((run (mapcar #'cl-third (cl-remove-if-not (lambda (l) (equal (car l) "compile"))
                                                    (svlsp-perf/log-lines)))))
      (svlsp-perf/row (if (bound-and-true-p lsp-headerline-breadcrumb-enable)
                          (format "edit -> diagnostics (breadcrumb flush %.1f s + 300 ms)"
                                  lsp-idle-delay)
                        (format "edit -> diagnostics (lsp-mode %.1f s + 300 ms)"
                                lsp-debounce-full-sync-notifications-interval))
                      "diagnostics" (nreverse samples) run nil 1))))

(defun svlsp-perf/typing (typing interval-ms)
  "Type TYPING's text a character every INTERVAL-MS, completing through capf
after each, as an auto-popup completion UI does."
  (let* ((pt (svlsp-perf/point-at (plist-get typing :at)))
         (text (plist-get typing :text))
         client run wait n)
    (with-current-buffer svlsp-perf/buf
      (save-excursion
        (goto-char pt)
        (dotimes (k (length text))
          (let ((c (aref text k)))
            (insert c)
            (svlsp-perf/keystroke-idle)
            (unless (memq c '(?\s ?\n ?\t))
              (svlsp-perf/log-lines)
              (let* ((t0 (float-time))
                     (capf (progn (lsp-completion--clear-cache) (lsp-completion-at-point)))
                     (count (length (all-completions "" (nth 2 capf))))
                     (ms (svlsp-perf/ms-since t0))
                     (srv (svlsp-perf/server-time "textDocument/completion")))
                (setq n count)
                (push ms client) (push (cdr srv) run) (push (car srv) wait)))
            (sit-for (/ interval-ms 1000.0))))
        (delete-region pt (+ pt (length text)))))
    (svlsp-perf/row (format "typing: capf per keystroke (%d ms apart)" interval-ms)
                    "keystroke" (nreverse client) (nreverse run) (nreverse wait) n)))

(defun svlsp-perf/run (scenario-file dir out repeat warmup interval-ms only)
  "Run SCENARIO-FILE in workspace DIR; write raw samples to OUT. t or a string."
  (condition-case err
      (let* ((scenario (with-temp-buffer
                         (insert-file-contents scenario-file)
                         (json-parse-buffer :object-type 'plist :array-type 'array
                                            :null-object nil :false-object :json-false)))
             (saved-log-io lsp-log-io)
             (settings (list :emacs emacs-version
                             :lsp_mode (or (ignore-errors
                                          (package-version-join
                                           (package-desc-version
                                            (cadr (assq 'lsp-mode package-alist)))))
                                         "")
                             :lsp_use_plists (if lsp-use-plists t :false)
                             :lsp_log_io :false
                             :native_json (if (fboundp (quote json-parse-string)) t :false)
                             :full_sync_debounce lsp-debounce-full-sync-notifications-interval
                             :idle_delay lsp-idle-delay
                             :headerline_breadcrumb (if (bound-and-true-p lsp-headerline-breadcrumb-enable) t :false)))
             phases rows)
        (setq lsp-log-io nil) ; a user's setting; logging every message costs
        (add-hook 'lsp-diagnostics-updated-hook #'svlsp-perf/on-diagnostics)
        (unwind-protect
            (progn
              (setq phases (svlsp-perf/open dir (plist-get scenario :open)
                                            (plist-get scenario :ready) 300))
              (setq rows (svlsp-perf/requests scenario repeat warmup only))
              (svlsp-perf/mem-phase "requests")
              (unless only
                (when-let ((edit (plist-get scenario :edit)))
                  (setq rows (append rows (list (svlsp-perf/edit edit repeat warmup))))
                  (svlsp-perf/mem-phase "edits (recompiles)"))
                (when-let ((typing (plist-get scenario :typing)))
                  (setq rows (append rows (list (svlsp-perf/typing typing interval-ms))))
                  (svlsp-perf/mem-phase "typing (capf + recompiles)")))
              (sit-for 2)
              (svlsp-perf/mem-phase "idle 2 s after")
              (with-temp-file out
                (insert (json-serialize
                         (list :scenario (plist-get scenario :name)
                               :client "emacs" :settings settings
                               :phases (vconcat (mapcar (lambda (p) (list :name (car p) :ms (cdr p)))
                                                        phases))
                               :rows (vconcat rows)
                               :memory (svlsp-perf/mem-summary)))))
              t)
          (remove-hook 'lsp-diagnostics-updated-hook #'svlsp-perf/on-diagnostics)
          (setq lsp-log-io saved-log-io)
          (when (buffer-live-p svlsp-perf/buf)
            (with-current-buffer svlsp-perf/buf (set-buffer-modified-p nil))
            (kill-buffer svlsp-perf/buf))
          (ignore-errors (lsp-workspace-folders-remove dir))))
    (error (format "elisp-error: %s" (error-message-string err)))))

;;; emacs_perf.el ends here
