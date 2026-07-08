// two_classes.sv — two classes with distinct members for scope-aware completion tests.

class ClassA;
    int alpha;
    int beta;
    function void method_a();
    endfunction
endclass

class ClassB;
    int gamma;
    int delta;
    function void method_b();
    endfunction
endclass
