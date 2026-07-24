#include <vector>
class Shape {
    public:
    double density() const;
    double l() const;
    double w() const;
    double d() const;
};

template <bool use_length, bool use_width, bool use_depth,
          bool use_volume, bool use_weight>
void measure(const std::vector<Shape>& VS,
        double* length, double* width, double* depth,
        double* volume, double* weight) {
  constexpr bool need_volume = use_volume || use_weight;
  constexpr bool need_l = use_length || need_volume;
  constexpr bool need_w = use_width  || need_volume;
  constexpr bool need_d = use_depth  || need_volume;
  double l, w, d, v;
  if constexpr (use_weight && !use_volume) volume = &v;
  if constexpr (need_volume) {
    if constexpr (!use_length) length = &l;
    if constexpr (!use_width ) width  = &w;
    if constexpr (!use_depth ) depth  = &d;
  }
  for (const Shape& S : VS) {
    if constexpr (need_l) *length = S.l();
    if constexpr (need_w) *width  = S.w();
    if constexpr (need_d) *depth  = S.d();
    if constexpr (need_volume) *volume = *length * *width * *depth;
    if constexpr (use_weight) *weight = *volume * S.density();
  }
}
void measure(const std::vector<Shape>& VS,
        double* length, double* width, double* depth,
        double* volume, double* weight) {
    const int key = ((length != nullptr) << 0) |
                    ((width  != nullptr) << 1) |
                    ((depth  != nullptr) << 2) |
                    ((volume != nullptr) << 3) |
                    ((weight != nullptr) << 4);
    switch (key) {
        case 0x01: measure<true , false, false, false, false>(VS, length, width, depth, volume, weight);
                   break;
        case 0x02: measure<false, true , false, false, false>(VS, length, width, depth, volume, weight);
                   break;
        case 0x04: measure<false, false, true , false, false>(VS, length, width, depth, volume, weight);
                   break;
        case 0x07: measure<true , true , true , false, false>(VS, length, width, depth, volume, weight);
                   break;
        case 0x08: measure<false, false, false, true , false>(VS, length, width, depth, volume, weight);
                   break;
        case 0x10: measure<false, false, false, false, true >(VS, length, width, depth, volume, weight);
                   break;
        case 0x18: measure<false, false, false, true , true >(VS, length, width, depth, volume, weight);
                   break;
        case 0x1f: measure<true , true , true , true , true >(VS, length, width, depth, volume, weight);
                   break;
        default:; // assert
    }
}
