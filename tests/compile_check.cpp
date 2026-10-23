#include "../core/fuzz.h"
int main(){ verdigris::Engine e; verdigris::Params p; e.setParams(p); e.prepare(44100); float a[64]={}, b[64]={}, l[64], r[64]; e.process(a,b,l,r,64); return 0; }
