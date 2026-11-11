#include "raycast_mesh.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <sstream>
#include <sys/resource.h>
using V = std::array<float,3>;
struct Mesh { std::vector<float> v; std::vector<unsigned> i; };
void require(bool b, const char* s) { if (!b) throw std::runtime_error(s); }
uint32_t word(const std::vector<unsigned char>& b, size_t p) {
 return uint32_t(b[p]) | uint32_t(b[p+1])<<8 | uint32_t(b[p+2])<<16 | uint32_t(b[p+3])<<24;
}
#include "../large-map-qualification/admission.h"
Mesh read(const char* path, const std::string& id, Envelope policy={}) {
 auto b=bounded(path,policy.bytes);
 require(b.size()>=84,"short header");
 require(std::memcmp(b.data(),"EQMESH01",8)==0 && word(b,8)==1,"schema");
 auto nv=word(b,12), ni=word(b,16);
 require(nv>0 && nv<=policy.nv && ni>0 && ni<=policy.ni && ni%3==0,"counts");
 require(b.size()==84+uint64_t(nv)*12+uint64_t(ni)*4,"length");
 require(id.size()==64 && id.find_first_not_of("0123456789abcdef")==std::string::npos,"identity syntax");
 require(std::memcmp(b.data()+20,id.data(),64)==0,"identity mismatch");
 if(!policy.mesh.empty()) require(nv==policy.nv && ni==policy.ni && b.size()==policy.bytes && digest(b)==policy.mesh,"policy mesh hash/counts/length");
 require(uint64_t(nv)*3<=std::numeric_limits<size_t>::max(),"vertex size overflow");
 Mesh m; m.v.reserve(size_t(nv)*3); m.i.reserve(ni);
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
  require(h.fields[3]==0 && h.fields[4]==0 && h.fields[5]==1,"analytic winding normal");
  require(!cast(*e,{{5,5,2},{5,5,-2},"miss"}).hit,"analytic miss");
  require(!cast(*e,{{1,1,2},{1,1,1},"short_miss"}).hit,"segment bound");
  require(!cast(*e,{{1,1,0},{1,1,0},"zero"}).hit,"zero ray");
  auto reverse=cast(*e,{{1,1,-2},{1,1,2},"reverse"}); require(reverse.hit && reverse.fields[6]==2,"two sided");
  auto bad=h; for(int k=0;k<7;k++) { bad=h; bad.fields[k]=std::nextafter(bad.fields[k],INFINITY); require(!equal(h,bad),"field comparator control"); }
  bad=h; bad.hit=false; require(!equal(h,bad),"hit comparator control");
  Mesh moved=m; for(size_t i=2;i<moved.v.size();i+=3) moved.v[i]+=10;
  Mesh reversed=m; std::swap(reversed.i[1],reversed.i[2]); auto rev=build(reversed);
  auto rh=cast(*rev,r); require(rh.hit && rh.fields[5]==-1,"reversed winding normal");
  auto other=build(moved); require(!equal(h,cast(*other,r)),"perturbed geometry sensitivity");
 }
 Mesh square{{0,0,0, 4,0,0, 4,4,0, 0,4,0}, {0,1,2,0,2,3}};
 auto seam=build(square);
 for(float x:{std::nextafter(2.0f,-INFINITY),2.0f,std::nextafter(2.0f,INFINITY)}) {
  auto h=cast(*seam,{{x,2,2},{x,2,-2},"seam"});
  require(h.hit && h.fields[2]==0 && h.fields[6]==2 && h.fields[5]==1,"analytic shared seam");
 }
 // A collinear face alongside the valid face must not invent a hit away from it.
 Mesh degenerate=m; degenerate.v.insert(degenerate.v.end(),{10,0,0,11,0,0,12,0,0});
 degenerate.i.insert(degenerate.i.end(),{3,4,5}); auto deg=build(degenerate);
 require(!cast(*deg,{{11,0,2},{11,0,-2},"degenerate"}).hit,"collinear triangle miss");
 std::cout<<"{\"controls\":32,\"perturbed_geometry_disagreements\":32}\n";
}

// Flush each phase immediately so a killed build still leaves its last boundary.
void phase(const char* name) {
 std::ifstream f("/proc/self/status"); std::string line;
 std::cerr<<"PHASE "<<name;
 while(std::getline(f,line)) if(line.rfind("VmRSS:",0)==0 || line.rfind("VmHWM:",0)==0) std::cerr<<" "<<line;
 for(const char* name:{"memory.current","memory.peak","memory.events"}) {
  std::ifstream c(std::string("/sys/fs/cgroup/")+name); std::string value((std::istreambuf_iterator<char>(c)),{});
  std::replace(value.begin(),value.end(),'\n',';'); std::cerr<<" "<<name<<"="<<value;
 }
 std::cerr<<std::endl;
}
float token(std::istream& in) {
 std::string s; require(bool(in>>s),"trace endpoint missing"); char* end=nullptr;
 float x=std::strtof(s.c_str(),&end);
 require(end==s.c_str()+s.size() && std::isfinite(x),"trace endpoint invalid"); return x;
}
// Each invocation owns one mesh/tree. Replay reads reference endpoints, never candidate geometry.
void single(int argc,char** argv) {
 require(argc==6 || (argc==9 && std::string(argv[6])=="--known-asset"),"usage: --record/--replay mesh identity reference-trace output-trace");
 bool record=std::string(argv[1])=="--record";
 phase("start"); auto policy=argc==9?envelope(argv[7],argv[8],argv[3]):Envelope{}; auto m=read(argv[2],argv[3],policy); phase("read");
 std::vector<Ray> q; if(record) q=queries(m);
 auto e=build(m); phase("build");
 m=Mesh{}; phase("arrays_released");
 std::ifstream input; if(!record) { input.open(argv[4]); require(bool(input),"trace input open"); }
 std::ofstream out(argv[5]); require(bool(out),"trace output open"); out<<std::hexfloat;
 auto emit=[&](const Ray& r) {
  auto h=cast(*e,r); out<<r.category;
  for(auto x:r.a) out<<' '<<x; for(auto x:r.b) out<<' '<<x;
  out<<' '<<h.hit; if(h.hit) for(auto x:h.fields) { require(std::isfinite(x),"nonfinite hit"); out<<' '<<x; }
  out<<'\n';
 };
 if(record) for(const auto& r:q) emit(r);
 else {
  std::string line;
  while(std::getline(input,line)) {
   std::istringstream row(line); Ray r; require(bool(row>>r.category),"trace category missing");
   for(auto& x:r.a) x=token(row); for(auto& x:r.b) x=token(row);
   // The independent comparator validates outcomes and row count.
   emit(r);
  }
  require(input.eof(),"trace read");
 }
 out.close(); require(bool(out),"trace write"); phase("query");
 e.reset(); q.clear(); q.shrink_to_fit(); phase("release");
}
int main(int argc,char** argv) { try {
 if(argc>1 && (std::string(argv[1])=="--record" || std::string(argv[1])=="--replay")) { single(argc,argv); return 0; }
 static_assert(sizeof(float)==4 && sizeof(unsigned)==4,"production layout");
 if(argc==2 && std::string(argv[1])=="--controls") { controls(); return 0; }
 require(argc==4,"usage: collision-eval cpp.mesh rust.mesh input-sha256 | --controls");
 auto a=read(argv[1],argv[3]),b=read(argv[2],argv[3]);
 require(a.v.size()==b.v.size() && a.i==b.i,"strict structure/index/order");
 auto x=build(a),y=build(b); auto q=queries(a);
 std::ofstream trace;
 if(const char* path=std::getenv("COLLISION_TRACE")) { trace.open(path); require(bool(trace),"trace open"); trace<<std::hexfloat; }
 struct Counts { unsigned total=0,hits=0,misses=0,differences=0; };
 std::map<std::string,Counts> counts; unsigned diffs=0,hits=0,misses=0; double maxdelta=0;
 for(const auto& r:q) { auto h=cast(*x,r),j=cast(*y,r); if(trace.is_open()) {
   trace<<r.category; for(auto f:r.a) trace<<' '<<f; for(auto f:r.b) trace<<' '<<f;
   for(const auto& out:{h,j}) { trace<<' '<<out.hit; if(out.hit) for(auto f:out.fields) trace<<' '<<f; }
   trace<<'\n';
  }
  auto& c=counts[r.category]; c.total++; (h.hit?c.hits:c.misses)++; (h.hit?hits:misses)++;
  if(!equal(h,j)) { c.differences++; diffs++; }
  if(h.hit && j.hit) for(int k=0;k<7;k++) { require(std::isfinite(h.fields[k])&&std::isfinite(j.fields[k]),"nonfinite hit field"); maxdelta=std::max(maxdelta,std::abs(double(h.fields[k])-j.fields[k])); }
 }
 std::cout<<"{\"seed\":2236555302,\"queries\":"<<q.size()<<",\"hits\":"<<hits<<",\"misses\":"<<misses<<",\"differences\":"<<diffs<<",\"max_absolute_field_delta\":"<<maxdelta<<",\"categories\":{";
 bool first=true; for(auto& kv:counts) { if(!first) std::cout<<","; first=false; auto& c=kv.second; std::cout<<"\""<<kv.first<<"\":{\"queries\":"<<c.total<<",\"hits\":"<<c.hits<<",\"misses\":"<<c.misses<<",\"differences\":"<<c.differences<<"}"; }
 std::cout<<"}}\n"; if(trace.is_open()) { trace.close(); require(bool(trace),"trace write"); } require(hits>=100 && misses>=100,"vacuous query set"); return diffs?1:0;
 } catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 2; } }
