==== Source: A.hyp ====
import "B.hyp";

library L {
    uint constant X = 1;
    uint constant Y = K.Y;
}

==== Source: B.hyp ====
import "A.hyp";

library K {
    uint constant X = L.X;
    uint constant Y = 2;
}

// ====
