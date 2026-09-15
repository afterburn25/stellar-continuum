#include <stellar/engine/native_map_platform.hpp>

#include <iostream>
#include <stdexcept>

int main(int argc,char**argv)try{
  if(argc!=2)throw std::invalid_argument("Usage: native_text_measure_tests <Rajdhani font>");
  stellar::native_map::Window window("Native text measurement",640,360,false,argv[1]);
  stellar::native_map::Text label{{},"PROXIMA CENTAURI  4.2 ly",{255,255,255,255},14};
  const auto before=window.text_cache_entries();const auto first=window.measure_text(label);const auto after=window.text_cache_entries();const auto repeated=window.measure_text(label);
  if(first.width<=0||first.height<=0||first.width!=repeated.width||first.height!=repeated.height)throw std::runtime_error("renderer text extent was empty or unstable");
  if(after!=before+1||window.text_cache_entries()!=after)throw std::runtime_error("text measurement rebuilt an existing cached layout");
  label.wrap_width=72;const auto wrapped=window.measure_text(label);if(wrapped.width>72||wrapped.height<=first.height)throw std::runtime_error("wrapped renderer extent did not match cached layout constraints");
  if(window.measure_text({}).width!=0||window.measure_text({}).height!=0)throw std::runtime_error("empty text did not measure to zero");
  std::cout<<"native exact cached text measurement passed\n";return 0;
}catch(const std::exception&error){std::cerr<<"native text measurement failed: "<<error.what()<<'\n';return 1;}
