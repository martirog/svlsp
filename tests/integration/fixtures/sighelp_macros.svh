// sighelp_macros.svh — `define-only header for sighelp_macros.sv (plan.md
// §6.29 part A): the macro's parameters must reach signature help through
// the included file, which has no symbols of its own.
`define SH29M_LOG(ID, MSG, VERB=1) $display("%s: %s (%0d)", ID, MSG, VERB)
