;;; emacs-test-init.el --- Minimal Emacs init for svlsp functional tests
;;
;; Loaded by the test daemon (tools/emacs-test-daemon.sh).
;; Packages must already be installed before this file is loaded —
;; installation is done by emacs-test-daemon.sh via emacs --batch.
;;
;; Required environment variables (set by emacs-test-daemon.sh):
;;   SVLSP_ROOT — absolute path to the project root
;;   SVLSP_BIN  — absolute path to the svlsp server binary (may not exist yet)

;;; Package loading -----------------------------------------------------

(let ((root (or (getenv "SVLSP_ROOT") default-directory)))
  (setq user-emacs-directory (expand-file-name ".emacs-test/" root))
  (setq package-user-dir     (expand-file-name ".emacs-test/elpa/" root)))

(require 'package)
(setq package-archives
      '(("melpa" . "https://melpa.org/packages/")
        ("gnu"   . "https://elpa.gnu.org/packages/")))
(package-initialize)   ; load already-installed packages; does not download

(require 'lsp-mode)

;;; lsp-mode settings ---------------------------------------------------

(setq lsp-enable-file-watchers       nil)  ; avoid inotify limits in tests
(setq lsp-restart                    'ignore) ; don't prompt on server crash
(setq lsp-log-io                     t)    ; keep full I/O log for diagnostics
(setq lsp-auto-guess-root            t)    ; don't prompt for workspace root
(setq lsp-enable-suggest-server-download nil) ; don't prompt to download servers
(setq lsp-warn-no-matched-clients    nil)  ; silence "no client" warnings

;;; clangd — used to validate the harness with a known-good LSP server ---

;; clangd-19 is installed as clangd-19, not clangd, on this machine.
(require 'lsp-clangd)
(setq lsp-clients-clangd-executable
      (or (executable-find "clangd")
          (executable-find "clangd-19")
          "clangd"))

;;; svlsp — registered for verilog-mode; binary may not exist yet -------

(defvar svlsp-test-server-bin
  (or (getenv "SVLSP_BIN")
      (expand-file-name "build/debug/svlsp"
                        (or (getenv "SVLSP_ROOT") default-directory)))
  "Path to the svlsp binary under test.")

(lsp-register-client
 (make-lsp-client
  :new-connection (lsp-stdio-connection (lambda () (list svlsp-test-server-bin)))
  :major-modes    '(verilog-mode)
  :server-id      'svlsp
  :priority        1))

;;; Test helper functions -----------------------------------------------

(defun svlsp-test/open-file (path)
  "Open PATH, activate its major mode and lsp-mode, return the buffer."
  (let ((buf (find-file-noselect (expand-file-name path))))
    (with-current-buffer buf
      (normal-mode)   ; activate the correct major mode for the file extension
      (lsp))
    buf))

(defun svlsp-test/wait-for-lsp (&optional timeout-secs)
  "Block until any LSP workspace is initialized or TIMEOUT-SECS pass.
Returns t on success, nil on timeout."
  (let ((deadline (+ (float-time) (or timeout-secs 30))))
    (while (and (not (lsp-workspaces))
                (< (float-time) deadline))
      (sit-for 0.5))
    ;; Give the workspace a moment to reach 'initialized state.
    (when (lsp-workspaces)
      (let ((deadline2 (+ (float-time) 10)))
        (while (and (< (float-time) deadline2)
                    (not (cl-some
                          (lambda (ws)
                            (eq (lsp--workspace-status ws) 'initialized))
                          (lsp-workspaces))))
          (sit-for 0.5))))
    (cl-some (lambda (ws)
               (eq (lsp--workspace-status ws) 'initialized))
             (lsp-workspaces))))

(defun svlsp-test/close-file (buf)
  "Kill buffer BUF without saving."
  (when (buffer-live-p buf)
    (with-current-buffer buf
      (set-buffer-modified-p nil))
    (kill-buffer buf)))

(message "svlsp-test: init complete; clangd=%s svlsp=%s"
         lsp-clients-clangd-executable
         svlsp-test-server-bin)
