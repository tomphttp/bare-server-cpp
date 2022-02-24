# bare-server-cpp

## Dependencies

- OpenSSL
```sh
sudo apt install libssl-dev
```

- Boost

`>= 1.72`

~~`sudo apt install libboost-dev`~~
Get latest version
```sh
wget https://downloads.sourceforge.net/project/boost/boost/1.78.0/boost_1_78_0.tar.gz
tar -zxvf boost_1_78_0.tar.gz
cd boost_1_78_0/
cpuCores=`cat /proc/cpuinfo | grep "cpu cores" | uniq | awk '{print $NF}'`
./bootstrap.sh 
sudo ./b2 --with=all -j $cpuCores install
cat /usr/local/include/boost/version.hpp | grep "BOOST_LIB_VERSION"
```

- RapidJSON
```sh
sudo apt install rapidjson-dev
```