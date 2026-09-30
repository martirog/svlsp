// callargs_scoped.sv — the missing-required-argument diagnostic through
// multi-level `::` qualifiers (the UVM factory shape, `T::type_id::create`).
// Used by test_39_missing_argument_diagnostic.sh: each class's `type_id` is a
// typedef of a registry, and only the component registry's cas_create
// requires `parent`. The whole qualifier decides which type_id is meant, so
// only the two cas_comp calls are reported; BUSTYPE is a type parameter and
// must fail closed rather than resolve to cas_pred's own (component) type_id
// (uvm_reg_predictor.svh:141).

package cas_pkg;
  class cas_obj_registry;
    static function int cas_create(string name = "", int parent = 0); return 0; endfunction
  endclass
  class cas_comp_registry;
    static function int cas_create(string name, int parent); return 0; endfunction
  endclass
  class cas_item;
    typedef cas_obj_registry type_id;
  endclass
  class cas_comp;
    typedef cas_comp_registry type_id;
  endclass
  class cas_pred #(type BUSTYPE = int);
    typedef cas_comp_registry type_id;
    static function void cas_run();
      void'(BUSTYPE::type_id::cas_create("t"));
      void'(cas_item::type_id::cas_create("i"));
      void'(cas_pkg::cas_item::type_id::cas_create("j"));
      void'(cas_comp::type_id::cas_create("c"));
      void'(cas_pkg::cas_comp::type_id::cas_create("d"));
    endfunction
  endclass
endpackage
