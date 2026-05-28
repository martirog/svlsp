// structs_unions.sv — structs and unions, used as a test fixture.
// Exercises: struct packed, struct unpacked, union packed, typedef, member access.

module structs_unions_demo;
    typedef struct packed {
        logic [3:0]  opcode;
        logic [3:0]  rd;
        logic [3:0]  rs;
        logic [11:0] imm;
        logic [7:0]  funct;
    } instr_t;

    typedef union packed {
        logic [31:0] word;
        struct packed {
            logic [15:0] hi;
            logic [15:0] lo;
        } halves;
        logic [3:0][7:0] bytes;
    } word32_t;

    typedef struct {
        string       name;
        int unsigned age;
        real         score;
    } person_t;

    instr_t  instr;
    word32_t w;
    person_t p;

    initial begin
        instr        = '0;
        instr.opcode = 4'hA;
        instr.rd     = 4'd3;
        instr.imm    = 12'hBEE;

        w.word = 32'hDEAD_BEEF;
        $display("hi=%04h lo=%04h", w.halves.hi, w.halves.lo);
        $display("bytes: %02h %02h %02h %02h",
                 w.bytes[3], w.bytes[2], w.bytes[1], w.bytes[0]);

        p.name  = "Alice";
        p.age   = 30;
        p.score = 99.5;
        $display("%s age=%0d score=%0.1f", p.name, p.age, p.score);
    end
endmodule
