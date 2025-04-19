#include <iostream> 
#include <thread> 
#include <mutex> 
int main() { 
   std::mutex mtx; 
   std::thread t([]{ std::cout << "Thread works!"; }); 
   t.join(); 
   return 0; 
} 
