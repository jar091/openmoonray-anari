// A loadable shared library that exports none of the rdl2 entry points.
// Stands in for an unrelated library dropped into the DSO directory.
extern "C" int openmoonray_anari_not_a_dso() { return 0; }
