// Reference-only IO adapter. Production C++ engine and existing driver remain unchanged.
#define main retained_driver_main
#include "../collision/driver.cpp"
#undef main
#include <chrono>
using Clock=std::chrono::steady_clock;
double seconds(Clock::time_point t) {return std::chrono::duration<double>(Clock::now()-t).count();}
void timed_phase(const char* name,Clock::time_point t) {std::cerr<<"SECONDS "<<name<<"="<<seconds(t)<<"\n";phase(name);}
int main(int argc,char** argv) {try {
 require(argc==5,"usage: mesh identity queries.bin output.bin");
 auto t=Clock::now();auto m=read(argv[1],argv[2]);timed_phase("read",t);
 t=Clock::now();auto e=build(m);m=Mesh{};timed_phase("build",t);
 std::ifstream in(argv[3],std::ios::binary);char header[76];require(bool(in.read(header,76)),"replay header");
 require(std::memcmp(header,"EQRAYS01",8)==0&&std::memcmp(header+8,argv[2],64)==0,"replay identity");
 uint32_t count;std::memcpy(&count,header+72,4);require(count>0&&count<=100000,"replay count");
 // Both harnesses are qualified only on the recorded little-endian x86_64 worker.
 std::ofstream out(argv[4],std::ios::binary);require(bool(out),"output open");
 double query=0,serialize=0;t=Clock::now();
 for(uint32_t ordinal=0;ordinal<count;ordinal++) {
  uint32_t id;Ray r;require(bool(in.read(reinterpret_cast<char*>(&id),4))&&id==ordinal,"replay order");
  require(bool(in.read(reinterpret_cast<char*>(r.a.data()),12))&&bool(in.read(reinterpret_cast<char*>(r.b.data()),12)),"replay endpoints");
  auto q=Clock::now();auto h=cast(*e,r);query+=seconds(q);
  q=Clock::now();unsigned char flag=h.hit;out.write(reinterpret_cast<char*>(&flag),1);
  if(h.hit) {out.write(reinterpret_cast<char*>(h.fields.data()),28);uint32_t unavailable=0xffffffff;out.write(reinterpret_cast<char*>(&unavailable),4);}serialize+=seconds(q);
 }
 require(in.peek()==std::char_traits<char>::eof(),"replay trailing");out.close();require(bool(out),"output write");timed_phase("query_and_serialize",t);
 std::cerr<<"{\"query_seconds\":"<<query<<",\"serialize_seconds\":"<<serialize<<",\"queries\":"<<count<<"}\n";
 e.reset();phase("release");return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 2;}}
