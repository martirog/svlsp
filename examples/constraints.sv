// constraints.sv — randomisation and constraints, used as a test fixture.
// Exercises: rand, randc, constraint, randomize(), inline constraint, std::randomize.

class Packet;
    rand  logic [7:0]  len;
    rand  logic [31:0] addr;
    randc logic [1:0]  prio;
    rand  logic [7:0]  payload [];

    constraint c_len {
        len inside {[4:64]};
    }

    constraint c_addr_align {
        addr[1:0] == 2'b00;
    }

    constraint c_prio_len {
        (prio == 2'b11) -> len > 32;
    }

    constraint c_payload_size {
        payload.size() == len;
    }

    function new();
        payload = new[64];
    endfunction
endclass

module constraint_demo;
    Packet pkt;
    int    rand_val;

    initial begin
        pkt = new();

        repeat (5) begin
            assert (pkt.randomize()) else $fatal(1, "randomize failed");
            $display("len=%0d addr=%08h prio=%0d", pkt.len, pkt.addr, pkt.prio);
        end

        // inline constraint
        assert (pkt.randomize() with { len == 8; prio == 2'b00; })
            else $fatal(1, "inline randomize failed");
        $display("fixed: len=%0d prio=%0d", pkt.len, pkt.prio);

        // std::randomize
        assert (std::randomize(rand_val) with { rand_val inside {[0:100]}; })
            else $fatal(1, "std::randomize failed");
        $display("rand_val=%0d", rand_val);
    end
endmodule
