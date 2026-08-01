int scores[5];
int i;
string label;
int total;

label = "Score Report";
print label;

i = 0;
while (i < 5) {
    scores[i] = i * 10;
    i = i + 1;
}

total = 0;
i = 0;
while (i < 5) {
    total = total + scores[i];
    i = i + 1;
}
print total;

int grade;
grade = 2;
switch (grade) {
    case 1:
        print "Excellent";
        break;
    case 2:
        print "Good";
        break;
    case 3:
        print "Average";
        break;
    default:
        print "Unknown";
}
