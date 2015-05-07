# ns3-tcpcompound

Introduction
------------
This is the implementation of TCP Compound for NS3. 

Install and Usage
-----------------
1. Download tcp-compound.cc and tcp-compound.h according to your NS3 version. I only test it in ns3.21 and ns3.22, but it should work well with older version (some minor modificaion might be needed though)

2. Placed the file downloaded to directory `/path/to/ns3-3.21(or 3.22)/src/internet/model`

3. Open `/path/to/ns3-3.21(or 3.22)/src/internet/wscript`, add `model/tcp-compound.cc` and `model/tcp-compound.h` to appropriate position

4. Build again, now you could run `test.cc` to see performance of TCP Compound

Should you have any questions, please contact zhou.fan1@husky.neu.edu
