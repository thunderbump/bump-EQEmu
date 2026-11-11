// Linux qualification adapter only. Hash the same bounded bytes that the reader parses.
#include <unistd.h>
#include <sys/wait.h>
#include <cerrno>
#include <limits>
static std::string digest(const std::vector<unsigned char>& b) {
 // A temporary file avoids pipe deadlocks and shell/path interpolation.
 auto file=std::unique_ptr<FILE,decltype(&fclose)>(tmpfile(),fclose);
 require(bool(file),"hash temporary file");
 require(fwrite(b.data(),1,b.size(),file.get())==b.size() && fflush(file.get())==0,"hash write");
 require(fseek(file.get(),0,SEEK_SET)==0,"hash rewind");
 int out[2]; require(pipe(out)==0,"hash pipe");
 auto pid=fork(); require(pid>=0,"hash fork");
 if(pid==0) { dup2(fileno(file.get()),0); dup2(out[1],1); close(out[0]); close(out[1]); execl("/usr/bin/sha256sum","sha256sum",(char*)nullptr); _exit(127); }
 close(out[1]); std::string result; char buf[128]; ssize_t n;
 while((n=::read(out[0],buf,sizeof(buf)))>0) result.append(buf,n);
 close(out[0]); int status=0; require(waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0 && result.size()>=64,"hash tool");
 return result.substr(0,64);
}
static std::vector<unsigned char> bounded(const char* path,size_t limit) {
 std::ifstream f(path,std::ios::binary); require(bool(f),"open mesh/input");
 std::vector<unsigned char> b; b.reserve(limit); char chunk[65536];
 while(f) { f.read(chunk,sizeof(chunk)); auto n=size_t(f.gcount()); require(n<=limit-b.size(),"mesh byte limit"); b.insert(b.end(),chunk,chunk+n); }
 require(f.eof(),"mesh/input read"); return b;
}
struct Envelope { size_t nv=4000000,ni=12000000,bytes=100000000; std::string mesh; };
static Envelope envelope(const std::string& name,const char* original,const std::string& id) {
 const char* manifest=
#include "assets.inc"
 ;
 std::istringstream lines(manifest); std::string line;
 while(std::getline(lines,line)) {
  std::istringstream in(line); std::vector<std::string> f; std::string field;
  while(std::getline(in,field,'|')) f.push_back(field);
  if(f.size()!=7 || f[0]!=name) continue;
  require(id==f[2],"policy input identity");
  auto b=bounded(original,std::stoull(f[1]));
  require(b.size()==std::stoull(f[1]) && digest(b)==id,"policy input hash/length");
  return {std::stoull(f[3]),std::stoull(f[4]),std::stoull(f[5]),f[6]};
 }
 throw std::runtime_error("unknown asset policy path");
}
