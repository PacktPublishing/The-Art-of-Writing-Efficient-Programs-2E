#include <vector>
class Shape {
    public:
    double density() const;
    double l() const;
    double w() const;
    double d() const;
};

void measure(const std::vector<Shape>& VS,
        double* length, double* width, double* depth,
        double* volume, double* weight) {
  for (const Shape& S : VS) {
    double l, w, d, v;
    if (weight && !volume) volume = &v;
    if (volume) {
      if (!length) length = &l;
      if (!width ) width  = &w;
      if (!depth ) depth  = &d;
    }
    if (length) *length = S.l();
    if (width ) *width  = S.w();
    if (depth ) *depth  = S.d();
    if (volume) *volume = *length * *width * *depth;
    if (weight) *weight = *volume * S.density();
  }
}
