// Semantic errors introduced by the advanced/unique extensions
// (arrays + switch) — see docs/advanced_features.md. Each of these
// should be reported with a line number, same as every other
// semantic error category in this compiler.

int arr[5];
int notAnArray;
int idx;
bool flagIdx;

// 1) indexing something that isn't an array
notAnArray[0] = 1;

// 2) array index must be type int
arr[flagIdx] = 1;

// 3) assigning a bool into an int array
arr[0] = true;

// 4) switch subject must be type int
bool b;
b = true;
switch (b) {
    case 1:
        print 1;
        break;
    default:
        print 0;
}

// 5) duplicate case label
int x;
x = 1;
switch (x) {
    case 1:
        print 1;
        break;
    case 1:
        print 2;
        break;
    default:
        print 3;
}
