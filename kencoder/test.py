import numpy as np
import kencoder
import sys

VAL0 = 90
VAL1 = 200
VAL2 = 175
VAL3 = 0
WIDTH = 640
HEIGHT = 480

def get_frame():
    qrow0 = np.ones(WIDTH//4, dtype='uint8')*VAL0
    qrow1 = np.ones(WIDTH//4, dtype='uint8')*VAL1
    qrow2 = np.ones(WIDTH//4, dtype='uint8')*VAL2
    qrow3 = np.ones(WIDTH//4, dtype='uint8')*VAL3
    hrow0 = np.append(qrow0, qrow0)
    hrow1 = np.append(qrow1, qrow1)
    hrow2 = np.append(qrow2, qrow2)
    hrow3 = np.append(qrow3, qrow3)
    row01 = np.append(hrow0, hrow1)
    row10 = np.append(hrow1, hrow0)
    hrow23 = np.append(qrow2, qrow3)
    hrow32 = np.append(qrow3, qrow2)
    hrow12 = np.append(qrow1, qrow2)
    hrow21 = np.append(qrow2, qrow1)
    row23 = np.append(hrow23, hrow23)
    row32 = np.append(hrow32, hrow32)
    row12 = np.append(hrow12, hrow12)
    row21 = np.append(hrow21, hrow21)

    frame = np.array([row01], dtype='uint8')

    for i in range(HEIGHT//2-1):
        frame = np.append(frame, [row01], axis=0)
    for i in range(HEIGHT//2):
        frame = np.append(frame, [row10], axis=0)
    for i in range(HEIGHT//8):
        frame = np.append(frame, [row23], axis=0)
    for i in range(HEIGHT//8):
        frame = np.append(frame, [row32], axis=0)
    for i in range(HEIGHT//8):
        frame = np.append(frame, [row23], axis=0)
    for i in range(HEIGHT//8):
        frame = np.append(frame, [row32], axis=0)

    return frame

file = open("out.h264", "wb")
f = get_frame()
e = kencoder.Encoder()
for i in range(600):
    print("encoding frame", i)
    d = e.encode((f, 12346666+i*16666, 567))
    file.write(d[0])

file.close()