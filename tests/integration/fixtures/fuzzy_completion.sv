// fuzzy_completion.sv — symbols and typed-prefix probe positions for
// fuzzy/typo-tolerant completion integration tests
// (test_25_completion_fuzzy.sh).
//
// Module "sensor" declares three symbols with different name shapes so a
// partial/typo'd prefix can be fuzzy-matched against them:
//   WIDTH      — parameter; also probed with its own exact full name "WIDTH"
//                as a regression baseline, and with the typo'd/skip-a-char
//                prefix "wdth"
//   report_id  — port; a contiguous, word-start match for prefix "rep"
//   xxrepxx    — port; only a scattered, mid-word match for the same "rep"
//
// The four "probe:" lines below put partial, otherwise-meaningless
// typed-prefix text at a precise (line, char) position for wordAtPosition
// to read. A never-defined `ifdef hides it from the parser (unlike a
// comment, completion still reads it), so it cannot be (mis)parsed as a
// declaration and create a spurious symbol of its own — an earlier version of this fixture used
// bare no-paren statements for the same purpose, but the grammar's
// documented data_type/variable_decl_assignment ambiguity parsed each one
// as an implicit-type variable declaration, making every probe's own typed
// text self-match in completion.
module sensor #(
    parameter int WIDTH = 8
) (
    input  logic report_id,
    input  logic xxrepxx
);
`ifdef SVLSP_TEST_PROBES
       probe: WIDTH
       probe: wdth
       probe: rep
       probe: qqqqq
`endif
endmodule
