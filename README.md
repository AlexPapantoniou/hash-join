Μέλη της ομάδας:
Παπαντωνίου Αλέξανδρος-Ιωάννης, ΑΜ: 1115202200139, email: sdi2200139@di.uoa.gr
Παπαιωάννου Πέτρος, ΑΜ: 1115202200137, email: sdi2200137@di.uoa.gr

Οδηγίες για build/run (αφου φτιάχτει η cache):
a. Για γενικά unit_tests και queries:

1. cmake --build build --target unchained
2. Για unit_tests: cmake --build build --target run_unit
   Για queries: cmake --build build --target queries

b. Για στοχευμένα unit_tests του κάθε αλγορίθμου:

cmake --build build --target run_<algorithm_name>
όπου algorithm_name τον αλγόριθμο κατακερματιμού που θέλουμε να τεστάρουμε:
i. "robinhood": robinhood
ii. "hopscotch": hopscotch
iii. "cuckoo": cuckoo
iv.  "uncained": unchained

c. Για τεστ άλλων αρχείων:

cmake --build build --target run_<test>
όπου test το τεστ του αρχείου που θέλουμε να τρέξουμε:
i. "columnar_utils": columnar_utils
ii. "column_store_utils": column_store_utils
iii. "slab_tests": slab_tests