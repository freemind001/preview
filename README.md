git clone https://github.com/freemind001/preview.git  
cd preview  
mkdir build  
cd build  
cmake ..  
make  
sudo make install  
sudo easy-switcher --configure  
sudo systemctl enable easy-switcher  
sudo systemctl start easy-switcher  
