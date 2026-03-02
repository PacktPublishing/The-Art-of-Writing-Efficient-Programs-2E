#include <iostream>

int i = 1;

int main() {
    std::cout << "Before" << std::endl;
    while (i) {}
    std::cout << "After" << std::endl;
}
