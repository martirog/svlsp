// classes.sv — OOP classes with inheritance, used as a test fixture.
// Exercises: class, extends, virtual, new, this, super, polymorphism.

class Packet;
    int unsigned id;
    logic [7:0]  payload [];

    function new(int unsigned id = 0, int unsigned size = 4);
        this.id      = id;
        this.payload = new[size];
    endfunction

    virtual function string to_string();
        return $sformatf("Packet[id=%0d, size=%0d]", id, payload.size());
    endfunction

    virtual function void fill(logic [7:0] val);
        foreach (payload[i]) payload[i] = val;
    endfunction
endclass

class ErrPacket extends Packet;
    logic [3:0] err_flags;

    function new(int unsigned id = 0, logic [3:0] flags = '0);
        super.new(id, 8);
        err_flags = flags;
    endfunction

    virtual function string to_string();
        return $sformatf("ErrPacket[id=%0d, flags=%04b]", id, err_flags);
    endfunction
endclass

class BurstPacket extends Packet;
    int unsigned burst_len;

    function new(int unsigned id = 0, int unsigned blen = 4);
        super.new(id, blen * 4);
        burst_len = blen;
    endfunction

    virtual function string to_string();
        return $sformatf("BurstPacket[id=%0d, burst=%0d]", id, burst_len);
    endfunction
endclass

module class_demo;
    Packet pkt_h;

    initial begin
        Packet      p = new(1);
        ErrPacket   e = new(2, 4'b1010);
        BurstPacket b = new(3, 8);

        $display("%s", p.to_string());
        $display("%s", e.to_string());
        $display("%s", b.to_string());

        // polymorphic handle
        pkt_h = e;
        $display("via base handle: %s", pkt_h.to_string());
    end
endmodule
