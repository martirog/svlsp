// sighelp_scoped.sv — fixture for test_12_signature_help.sh: calls through
// multi-level `::` qualifiers (the UVM factory shape, `T::type_id::create`).
// shsc_item's type_id is the object registry, shsc_pred's own is the
// component registry; the whole qualifier decides which one a call means,
// and a type parameter's (BUSTYPE) resolves to nothing. The cursor is on
// each call's second argument.

package shsc_pkg;
  class shsc_obj_registry;
    static function int shsc_create(string shsc_name = "", int shsc_parent = 0); return 0; endfunction
  endclass
  class shsc_comp_registry;
    static function int shsc_create(string shsc_name, int shsc_parent, int shsc_ctx = 0); return 0; endfunction
  endclass
  class shsc_item;
    typedef shsc_obj_registry type_id;
  endclass
  class shsc_pred #(type BUSTYPE = int);
    typedef shsc_comp_registry type_id;
    static function void shsc_run();
      int x;
      x = shsc_item::type_id::shsc_create("i", 1);
      x = shsc_pkg::shsc_item::type_id::shsc_create("j", 2);
      x = BUSTYPE::type_id::shsc_create("t", 3);
    endfunction
  endclass
endpackage
