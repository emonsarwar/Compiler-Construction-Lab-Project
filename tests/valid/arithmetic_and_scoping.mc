// Valid program #2 — a more elaborate non-trivial program.
// Exercises: float arithmetic with implicit int->float widening,
// nested if/else-if chains (via nested blocks), logical &&/||/!,
// modulo, unary negation, and variable shadowing in a nested block.

int n;
int i;
int sumOfEvens;
float average;
bool done;

n = 10;
i = 0;
sumOfEvens = 0;
done = false;

while (i < n && !done) {
    if (i % 2 == 0) {
        sumOfEvens = sumOfEvens + i;
    } else {
        // odd — skip
        sumOfEvens = sumOfEvens + 0;
    }
    if (i == 8) {
        done = true;
    }
    i = i + 1;
}

print sumOfEvens;

average = sumOfEvens / 2.0;   // int -> float widening in the division
print average;

int threshold;
threshold = 5;
if (sumOfEvens > threshold || sumOfEvens == 0) {
    print true;
} else {
    print false;
}

{
    // a block-local variable shadows the outer `threshold`
    bool threshold;
    threshold = false;
    print threshold;
}

print threshold;   // outer int `threshold` is visible again here

float negResult;
negResult = -average;
print negResult;
