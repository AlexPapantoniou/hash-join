Μέλη της ομάδας:
Παπαντωνίου Αλέξανδρος-Ιωάννης, ΑΜ: 1115202200139, email: sdi2200139@di.uoa.gr
Παπαιωάννου Πέτρος, ΑΜ: 1115202200137, email: sdi2200137@di.uoa.gr

Οδηγίες για build/run (αφου φτιάχτει η cache):
a. Για γενικά unit_tests και queries:

1. cmake --build build --target <algorithm_name>
   όπου algorithm_name τον αλγόριθμο κατακερματιμού που θα χρησιμοποιήσουμε:
   i. "unordered": std::unordered_map
   ii. "robinhood": robinhood
   iii. "hopscotch": hopscotch
   iv. "cuckoo": cuckoo
2. Για unit_tests: cmake --build build --target run_unit
   Για queries: cmake --build build --target queries

b. Για στοχευμένα unit_tests του κάθε αλγορίθμου:

cmake --build build --target run\_<algorithm_name>
όπου algorithm_name τον αλγόριθμο κατακερματιμού που θέλουμε να τεστάρουμε:
i. "robinhood": robinhood
ii. "hopscotch": hopscotch
iii. "cuckoo": cuckoo

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

---

---

---

---

---

---

Παραδοτέο 2:

Χρόνοι εκτέλεσης των queries άσκηση 1.1:

std::unordered_map: 155000ms
robinhood: 144000ms
hopscotch: 210000ms
cuckoo: 180000ms

Χρόνοι εκτέλεσης των queries άσκηση 1.2:

std::unordered_map: 130000ms
robinhood: 120000ms
hopscotch: 170000ms
cuckoo: 150000ms

Χρόνοι εκτέλεσης των queries άσκηση 2:

std::unordered_map: 58000ms
robinhood: 77000ms
hopscotch: 98000ms
cuckoo: 69000ms

Άσκηση 3:

Δομή unchained hash table:

1. Directory: Αποτελείται από directory entries, οι οποίοι είναι αριθμόι 64-bit και αποτελλούνται από έναν 48-bit 'δείκτη' (index)
   που δείχνει σε ποια θέση του πίνακα με τα ζευγάρια key-value ξεκινάει η δεικτιοδότηση του συγκεκριμένου slot και από ένα 16-bit φίλτρο
   (bloom filter) το οποίο χρησιμοποιείται για την γρήγορη απόρριψη κλειδιών κατά την αναζήτηση τα οποία δεν μπορεί να υπάρχουν στο hash table.
2. Tuples: Ένας πίνακας με τα ζευγάρια key-value ταξινομημένα με βάση το hash των κλειδιών.

Επειδή η αλλαγή του directory είναι χρονοβόρα (query 2a: 47000ms), δεν γίνεται σε κάθε προσθήκη στοιχείου στον πίνακα. Αντίθετα, όταν προσθέτουμε
ένα καινούριο στοιχείο δεν αλλάζουμε το directory, απλά το εισάγονται στο τέλος του πίνακα με τα tuples. Όταν έχουμε προσθέσει όλα τα στοιχεία που
θέλουμε (κατά το build phase του join algorithm), καλούμε την create_directory() η οποία ταξινομεί τα tuples και δημιουργεί τα directory entries
για να χρησιμοποιηθούν στην αναζήτηση (κατά το probe phase του join algorithm). Τυχών διπλότυπα κλειδιά εισάγονται πολλές φορές στον πίνακα.

Χρόνος εκτέλεσης των queries:

unchained: 60000ms
