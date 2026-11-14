// imp_api.sv — fixture for test_57_import_check.sh (plan.md §6.37): the
// package imp_use.sv imports from. It declares imp_item, not imp_missing.

package imp_api_pkg;
  class imp_item; endclass
endpackage
