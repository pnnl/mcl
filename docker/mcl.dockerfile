FROM ubuntu:latest

RUN apt-get update
RUN apt-get -y upgrade
RUN apt-get update --fix-missing
RUN apt-get -y install apt-utils psmisc
RUN apt-get -y install emacs locate
RUN apt-get -y install git
RUN apt-get -y install autotools-dev
RUN apt-get -y install dh-autoreconf
RUN apt-get -y install ocl-icd* opencl-headers clinfo 
RUN apt-get -y install openmpi-bin openmpi-common openmpi-doc
RUN apt-get -y install libpocl2 nvidia-cuda-toolkit
COPY mcl/. /root/
COPY uthash/src/. /usr/include/