// Alphabetically first path in this fixture directory, so a same-named
// symbol here would win findSymbolsByName's ORDER BY path, line tiebreak
// if the caller had no kind preference. Declares "DisambigTarget" as a
// plain Signal — pickBestSymbol() must NOT let this win over the real
// Class of the same name in zzz_class_decl.sv (see test_26).
module aaa_signal_holder;
  logic DisambigTarget;
endmodule
