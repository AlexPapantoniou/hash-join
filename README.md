Μέλη της ομάδας:
Παπαντωνίου Αλέξανδρος-Ιωάννης, ΑΜ: 1115202200139, email: sdi2200139@di.uoa.gr
Παπαιωάννου Πέτρος, ΑΜ: 1115202200137, email: sdi2200137@di.uoa.gr

Οδηγίες για build/run (αφου φτιάχτει η cache):
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

std::unordered_map: 230000ms
robinhood: 230000ms
hopscotch: 224000ms
cuckoo: 227000ms

Με τι ασχολήθηκε το κάθε μέλος της ομάδας:

Παπαντωνίου Αλέξανδρος: Robinhood hashmap, Robinhood tests, Cuckoo hashmap
Παπαιωάννου Πέτρος: Hopscotch hashmap, Hopscotch tests, Cuckoo tests

Και τα δύο μέλη ασχοληθήκαμε με το optimization των αλγορίθμων, τον σχολιασμό του κώδικα
και την συγγραφή του αρχείου README.

Σχετικά με τα optimization:

1. Οι αλγόριθμοι Robinhood και Hopscotch εκτελούν rehash όταν το load factor γίνει 0.75,
   ενώ ο αλγόριθμος cuckoo εκτελεί rehash όταν το load factor γίνει 0.90.
2. Αντί για την std::hash, χρησιμοποιήσαμε μία συνάρτηση mix_hash, η οποία κατανέμει καλύτερα
   τα στοιχεία στον πίνακα (πηγή: https://stackoverflow.com/questions/8509180/hashing-a-small-number-to-a-random-looking-64-bit-integer).
3. Χρήση δεύτερης emplace για το rehash η οποία δεν αντιγράφει το κλειδί και το value αλλά το μετακινεί
   (χρήση std::move).

Με την εφαρμογή των παραπάνω, παρατηρήσαμε πτώση του χρόνου εκτέλεσης σε περίπου 230000ms σε όλες τις περιπτώσεις.
