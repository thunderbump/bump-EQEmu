#include "raycast_mesh.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using V = std::array<float,3>;
struct Mesh { std::vector<float> v; std::vector<unsigned> i; };
void require(bool b, const char* s) { if (!b) throw std::runtime_error(s); }
uint32_t word(const std::vector<unsigned char>& b, size_t p) {
 return uint32_t(b[p]) | uint32_t(b[p+1])<<8 | uint32_t(b[p+2])<<16 | uint32_t(b[p+3])<<24;
}
Mesh read(const char* path, const std::string& id) {
 std::ifstream f(path,std::ios::binary); require(bool(f),"open mesh");
 std::vector<unsigned char> b{std::istreambuf_iterator<char>(f),{}};
 require(b.size()>=84,"short header");
 require(std::memcmp(b.data(),"EQMESH01",8)==0 && word(b,8)==1,"schema");
 auto nv=word(b,12), ni=word(b,16);
 require(nv>0 && nv<=4000000 && ni>0 && ni<=12000000 && ni%3==0,"counts");
 require(b.size()==84+uint64_t(nv)*12+uint64_t(ni)*4,"length");
 require(id.size()==64 && id.find_first_not_of("0123456789abcdef")==std::string::npos,"identity syntax");
 require(std::memcmp(b.data()+20,id.data(),64)==0,"identity mismatch");
 Mesh m;
 for(size_t p=84;p<84+size_t(nv)*12;p+=4) { auto u=word(b,p); float x; std::memcpy(&x,&u,4); require(std::isfinite(x),"nonfinite vertex"); m.v.push_back(x); }
 for(size_t p=84+size_t(nv)*12;p<b.size();p+=4) { auto i=word(b,p); require(i<nv,"index bounds"); m.i.push_back(i); }
 return m;
}
struct Release { void operator()(RaycastMesh* p) const { if(p) p->release(); } };
using Engine=std::unique_ptr<RaycastMesh,Release>;
Engine build(const Mesh& m) { return Engine(createRaycastMesh(m.v.size()/3,m.v.data(),m.i.size()/3,m.i.data())); }
struct Ray { V a,b; std::string category; };
struct Hit { bool hit; std::array<float,7> fields{}; };
Hit cast(RaycastMesh& m,const Ray& r) { Hit h; h.hit=m.raycast(r.a.data(),r.b.data(),h.fields.data(),h.fields.data()+3,h.fields.data()+6); return h; }
bool equal(const Hit& a,const Hit& b) { return a.hit==b.hit && (!a.hit || std::memcmp(a.fields.data(),b.fields.data(),28)==0); }
V add(V a,V b,float scale=1) { for(int k=0;k<3;k++) a[k]+=scale*b[k]; return a; }
V vertex(const Mesh& m,unsigned i) { return {m.v[3*i],m.v[3*i+1],m.v[3*i+2]}; }
// Explicit integer PRNG and 24-bit conversion avoid library distribution differences.
uint32_t state=0x854f2026;
float uniform() { state^=state<<13; state^=state>>17; state^=state<<5; return float(state>>8)/16777216.0f; }
std::vector<Ray> queries(const Mesh& m) {
 V lo=vertex(m,0),hi=lo;
 for(size_t i=0;i<m.v.size()/3;i++) for(int k=0;k<3;k++) { lo[k]=std::min(lo[k],m.v[3*i+k]); hi[k]=std::max(hi[k],m.v[3*i+k]); }
 std::vector<Ray> q;
 auto point=[&] { V p; for(int k=0;k<3;k++) p[k]=lo[k]-10+uniform()*(hi[k]-lo[k]+20); return p; };
 for(int i=0;i<2000;i++) q.push_back({point(),point(),"spatial"});
 for(int i=0;i<1000;i++) { auto a=point(),b=a; a[2]=hi[2]+10; b[2]=lo[2]-10; q.push_back({a,b,"vertical"}); }
 for(int i=0;i<200;i++) { V a=hi,b=hi; for(int k=0;k<3;k++) { a[k]+=100+i; b[k]+=101+i; } q.push_back({a,b,"outside"}); }
 size_t nt=m.i.size()/3;
 for(size_t s=0;s<256;s++) {
  size_t t=s*(nt-1)/255;
  V a=vertex(m,m.i[t*3]),b=vertex(m,m.i[t*3+1]),c=vertex(m,m.i[t*3+2]);
  V u=add(b,a,-1),v=add(c,a,-1),n={u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
  float len=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]); if(!std::isfinite(len)||len<1e-8f) continue;
  for(auto& x:n) x/=len;
  V center,edge; for(int k=0;k<3;k++) { center[k]=a[k]/3+b[k]/3+c[k]/3; edge[k]=a[k]/2+b[k]/2; }
  for(auto entry:std::vector<std::pair<V,std::string>>{{center,"interior"},{edge,"edge"},{a,"vertex"}}) {
   q.push_back({add(entry.first,n,2),add(entry.first,n,-2),entry.second});
   auto p=entry.first; p[0]=std::nextafter(p[0],INFINITY);
   q.push_back({add(p,n,2),add(p,n,-2),entry.second+"_adjacent"});
  }
  q.push_back({add(center,n,.001f),add(center,n,-.001f),"short"});
  V tangent=u; float d=std::sqrt(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]); if(d>0) {
   for(auto& x:tangent) x/=d;
   q.push_back({add(add(center,tangent,-2),n,.00001f),add(add(center,tangent,2),n,-.00001f),"near_parallel"});
  }
  q.push_back({center,center,"zero_length"});
 }
 return q;
}
void controls() {
 Mesh m{{0,0,0, 4,0,0, 0,4,0},{0,1,2}};
 Ray r{{1,1,2},{1,1,-2},"analytic"};
 for(int j=0;j<32;j++) {
  auto e=build(m); auto h=cast(*e,r); require(h.hit,"analytic hit");
  require(h.fields[0]==1 && h.fields[1]==1 && h.fields[2]==0 && h.fields[6]==2,"analytic location/distance");
  require(h.fields[3]==0 && h.fields[4]==0 && h.fields[5]==-1,"analytic winding normal");
  require(!cast(*e,{{5,5,2},{5,5,-2},"miss"}).hit,"analytic miss");
  require(!cast(*e,{{1,1,2},{1,1,1},"short_miss"}).hit,"segment bound");
  require(!cast(*e,{{1,1,0},{1,1,0},"zero"}).hit,"zero ray");
  auto reverse=cast(*e,{{1,1,-2},{1,1,2},"reverse"}); require(reverse.hit && reverse.fields[6]==2,"two sided");
  auto bad=h; bad.fields[6]=std::nextafter(bad.fields[6],INFINITY); require(!equal(h,bad),"field comparator control");
  bad=h; bad.hit=false; require(!equal(h,bad),"hit comparator control");
  Mesh moved=m; for(size_t i=2;i<moved.v.size();i+=3) moved.v[i]+=10;
  auto other=build(moved); require(!equal(h,cast(*other,r)),"perturbed geometry sensitivity");
 }
 std::cout<<"{\"controls\":32,\"perturbed_geometry_disagreements\":32}\n";
}
int main(int argc,char** argv) { try {
 static_assert(sizeof(float)==4 && sizeof(unsigned)==4,"production layout");
 if(argc==2 && std::string(argv[1])=="--controls") { controls(); return 0; }
 require(argc==4,"usage: collision-eval cpp.mesh rust.mesh input-sha256 | --controls");
 auto a=read(argv[1],argv[3]),b=read(argv[2],argv[3]);
 require(a.v.size()==b.v.size() && a.i==b.i,"strict structure/index/order");
 auto x=build(a),y=build(b); auto q=queries(a);
 struct Counts { unsigned total=0,hits=0,misses=0,differences=0; };
 std::map<std::string,Counts> counts; unsigned diffs=0,hits=0,misses=0; double maxdelta=0;
 for(const auto& r:q) { auto h=cast(*x,r),j=cast(*y,r); auto& c=counts[r.category]; c.total++; (h.hit?c.hits:c.misses)++; (h.hit?hits:misses)++;
  if(!equal(h,j)) { c.differences++; diffs++; }
  if(h.hit && j.hit) for(int k=0;k<7;k++) { require(std::isfinite(h.fields[k])&&std::isfinite(j.fields[k]),"nonfinite hit field"); maxdelta=std::max(maxdelta,std::abs(double(h.fields[k])-j.fields[k])); }
 }
 std::cout<<"{\"seed\":2236555302,\"queries\":"<<q.size()<<",\"hits\":"<<hits<<",\"misses\":"<<misses<<",\"differences\":"<<diffs<<",\"max_absolute_field_delta\":"<<maxdelta<<",\"categories\":{";
 bool first=true; for(auto& kv:counts) { if(!first) std::cout<<","; first=false; auto& c=kv.second; std::cout<<"\""<<kv.first<<"\":{\"queries\":"<<c.total<<",\"hits\":"<<c.hits<<",\"misses\":"<<c.misses<<",\"differences\":"<<c.differences<<"}"; }
 std::cout<<"}}\n"; require(hits>=100 && misses>=100,"vacuous query set"); return diffs?1:0;
 } catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 2; } }
