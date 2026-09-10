import encodings
import json
import sys

print("Python", sys.version.split()[0])
print("Hello from Python 3 on LeonOS!")
numbers = list(range(1, 11))
print("Numbers:", numbers)
print("Sum:", sum(numbers))
a, b = 0, 1
fibonacci = []
for _ in range(10):
    fibonacci.append(a)
    a, b = b, a + b
print("Fibonacci:", fibonacci)
print("JSON:", json.dumps({"sum": sum(numbers)}))
