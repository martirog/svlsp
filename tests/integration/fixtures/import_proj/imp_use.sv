// imp_use.sv — fixture for test_57_import_check.sh (plan.md §6.37):
// imp_item is declared in imp_api_pkg, imp_missing is not.

package imp_use_pkg;
  import imp_api_pkg::imp_item;
  import imp_api_pkg::imp_missing;
endpackage
