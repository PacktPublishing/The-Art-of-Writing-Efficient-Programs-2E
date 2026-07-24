#include <vector>

enum op_t { do_shrink, do_grow };
class Shape {
    public:
    void shrink();
    void grow();
};

void process(std::vector<Shape>& v, op_t op) {
  for (Shape& s : v) {
    if (op == do_shrink) s.shrink();
    else s.grow();
  }
}
