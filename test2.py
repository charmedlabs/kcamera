import kcamera
import kencoder
import time
import cv2 

N = 600

c = kcamera.Camera()
e = kencoder.Encoder()
s = c.stream()
file = open("out.h264", "wb")

f = s.frame()
pts0 = f[1]

t0 = time.time()
for i in range(N):
    f = s.frame()
    d = e.encode((cv2.cvtColor(f[0], cv2.COLOR_BGR2YUV_I420), f[1], f[2]))
    file.write(d[0])
    print(i, f[0].shape, f[1]-pts0, f[1], pts0)
    pts0 = f[1]
    if d[1]!=f[1]:
        print("*****", d[1], f[1])

print('framerate:', N/(time.time()-t0), 'fps')

file.close()