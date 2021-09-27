import kcamera
import time

N = 600

c = kcamera.Camera()
s = c.stream()
f = s.frame()
pts0 = f[1]

t0 = time.time()
for i in range(N):
    f = s.frame()
    if f[1]<0:
        print("break", f[1])
        break
    print(i, f[0].shape, f[1]-pts0)
    pts0 = f[1]

print('framerate:', N/(time.time()-t0), 'fps')
