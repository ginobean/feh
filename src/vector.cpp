#include <bits/stdc++.h>

using namespace std;




int test_cpp_func() {

    // using brace style initialization to initialize a vector
    vector<string> v1 { "uno", "dos", "tres" };

    cout << "v1[0] == " << v1[0] << endl;
    cout << "v1[2] == " << v1[2] << endl;

    // interestingly, on my 64 bit linux machine the sizes of 'long' and
    // 'long long' types are BOTH 8 bytes!
    // It's very possible though that on 32 bit machines and devices (phones) that
    // they might be different sizes, although both Apple and Google seem
    // to have dropped support for 32 bit devices anyways, so this is probably
    // a moot point.
    // 'long's and 'long long's will probably be 8 bytes (64 bits) long for the
    // foreseeable future.
    //
    long l { 77 };
    cout << "sizeof long type is " << sizeof(l) << endl;

    long ll { 88 };
    cout << "sizeof long long type is " << sizeof(ll) << endl;

    return 0;
}
