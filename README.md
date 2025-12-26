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
(Edit: Παρατηρήσαμε ότι οι χρόνοι που είχαμε υπολογίσει δεν ήταν ακριβείς. Προσθέσαμε τους χρόνους από τα πειράματα που κάναμε τώρα)

std::unordered_map: 230000ms -> 280000ms
robinhood: 230000ms -> 280000ms
hopscotch: 224000ms -> 265000ms
cuckoo: 227000ms -> 270000ms

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

Άσκηση 1:

Δομή value_t:

Περιέχει έναν αριθμό 64-bit ο οποίος μπορεί να αναπαραστήσει ταυτόχρονα INT32, VARCHAR και NULL με τον παρακάτω τρόπο:

1. Τα τελευταία 3 bit (least significant) δείχνουν τον τύπο των δεδομένων που αναπαριστά.
2. Τα υπόλοιπα 61 έχουν τις πληροφορίες που χρειάζεται για την σωστή αναπαράσταση των δεδομένων:
   a. Για INT32 κρατάμε τον αριθμό (δεν χρειάζεται να κωδικοποιήσουμε έναν απλό ακέραιο).
   b. Για VARCHAR κρατάμε ένα string representation το οποίο περιέχει τις παρακάτω πληροφορίες:
   i. table_id: Σε ποιον πίνακα βρίσκεται το string.
   ii. column_id: Σε ποια στήλη του πίνακα βρίσκεται το string.
   iii. page_id: Σε ποια σελίδα της στήλης βρίσκεται το string.
   iv. offset: Το offset μέσα στην σελίδα που τελειώνει το string (Όπως το διαβάζουμε από τον πίνακα εισόδου).
   c. Για NULL θέτουμε την τιμή του value_t σε 0.

Χρόνοι εκτέλεσης των queries (1.1):

std::unordered_map: 152000ms
robinhood: 144000ms
hopscotch: 210000ms
cuckoo: 180000ms

Χρόνοι εκτέλεσης των queries (1.2):

std::unordered_map: 130000ms
robinhood: 120000ms
hopscotch: 170000ms
cuckoo: 150000ms

Άσκηση 2:

Δομή column_t:

Περιέχει έναν πίνακα με σελίδες από value_t που περιέχονται σε μία στήλη ενός πίνακα καθώς και τον αριθμό των σειρών στην στήλη.

Χρόνοι εκτέλεσης των queries:

std::unordered_map: 55000ms
robinhood: 67000ms
hopscotch: 86000ms
cuckoo: 66000ms

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

Η αναζήτηση στο unchained hashmap γίνεται στα εξής δύο βήματα από τον χρήστη:

1. Αρχικά καλεί την συνάρτηση lookup_range(key) η οποία επιστρέφει τις θέσεις στον πίνακα που αντιστοιχούν στο κλειδί που ψάχνει.
2. Στην συνέχεια χρησιμοποιεί αυτό το range για να ψάξει τις ανάμεσα θέσεις για το κλειδί.

Χρόνος εκτέλεσης των queries:

crc32: 56000ms
fibonacci: 60000ms
