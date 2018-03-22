# CompressMPI
Adds four API calls to MPI to allow online and offline compression

# Steps for building and installing
1. Clone this repo: ```git clone https://github.com/jangeja94/CompressMPI.git CompressMPI```
2. ```bash setup.bash```
3. Add the lines to your .bashrc or .bash_profile that were output at the end of setup.bash
5. ```source ~/.bashrc``` or ```source ~/.bash_profile```
4. Now everything should be built and compiled. Use mpi normally and you should be able to call MPI_Csend, MPI_Crecv, MPI_Csend_Online, MPI_Crecv_online from within your c files.
