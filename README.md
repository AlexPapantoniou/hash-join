Μέλη της ομάδας:
Παπαντωνίου Αλέξανδρος-Ιωάννης, ΑΜ: 1115202200139, email: sdi2200139@di.uoa.gr
Παπαιωάννου Πέτρος ΑΜ: 1115202200137, email: sdi2200137@di.uoa.gr

Οδηγίες για build/run:
a. Για γενικά unit_tests και queries:

1. cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHASH_ALGORITHM=<algorithm_name> -Wno-dev
   όπου algorithm_name τον αλγόριθμο κατακερματιμού που θα χρησιμοποιήσουμε:
   i. "standard": std::unordered_map
   ii. "robinhood": robinhood
   iii. "hopscotch": hopscotch
   iv. "cuckoo": cuckoo
2. Για unit_tests: cmake --build build --target unit_tests -j 3
   Για queries: cmake --build build -- -j 3 fast
3. Για unit_tests: ./build/unit_tests
   Για queries: ./build/fast plans.json

b. Για στοχευμένα unit_tests του κάθε αλγορίθμου:

1. cmake --build build --target <algorithm_name>\_tests -j 3
   όπου algorithm_name τον αλγόριθμο κατακερματιμού που θέλουμε να τεστάρουμε:
   i. "robinhood": robinhood
   ii. "hopscotch": hopscotch
   iii. "cuckoo": cuckoo
2. ./build/<algorithm_name>\_tests

Χρόνοι εκτέλεσης των queries:

std::unordered_map: 230110 ms
robinhood: 231420 ms
hopscotch: 224150 ms
cuckoo: 227788 ms
