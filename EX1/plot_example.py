# OS 24 EX1
import pandas as pd
import matplotlib.pyplot as plt

data = pd.read_csv(r"/Users/yotamsu/Desktop/OS/EX1/memory_latency.csv")
data = data.to_numpy()

l1_size = 32000
l2_size = 256000
l3_size = 16000000

plt.plot(data[:, 0], data[:, 1], label="Random access")
plt.plot(data[:, 0], data[:, 2], label="Sequential access")
plt.xscale('log')
plt.yscale('log')
plt.axvline(x=l1_size, label="L1 (32KB)", c='r')
plt.axvline(x=l2_size, label="L2 (256KB)", c='g')
plt.axvline(x=l3_size, label="L3 (16MB)", c='brown')
plt.legend()
plt.title("Latency as a function of array size")
plt.ylabel("Latency (ns log scale)")
plt.xlabel("Bytes allocated (log scale)")
plt.show()
