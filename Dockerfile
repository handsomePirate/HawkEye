FROM gcc:latest

WORKDIR /usr/app
COPY ./ .
RUN mkdir -p premake
RUN mkdir -p vulkansdk

WORKDIR /usr/app/premake
RUN wget --no-check-certificate https://github.com/premake/premake-core/releases/download/v5.0.0-beta1/premake-5.0.0-beta1-linux.tar.gz && \
 tar xvzf premake-5.0.0-beta1-linux.tar.gz && \
 rm premake-5.0.0-beta1-linux.tar.gz
 
WORKDIR /usr/app/vulkansdk
RUN wget --no-check-certificate https://sdk.lunarg.com/sdk/download/1.3.204.0/linux/vulkansdk-linux-x86_64-1.3.204.0.tar.gz && \
 tar xvzf vulkansdk-linux-x86_64-1.3.204.0.tar.gz && \
 rm vulkansdk-linux-x86_64-1.3.204.0.tar.gz
 
ENV OLDPATH="${PATH}"
ENV PATH="/usr/app/premake:${OLDPATH}"
ENV VULKAN_SDK="/usr/app/vulkansdk/1.3.204.0/x86_64"

ENV LD_LIBRARY_PATH="/usr/app/vulkansdk/1.3.204.0/x86_64/lib"
ENV VK_LAYER_PATH="/usr/app/vulkansdk/1.3.204.0/x86_64/etc/vulkan/explicit_layer.d"

WORKDIR /usr/app/sln
RUN apt install libx11-dev
RUN premake5 gmake2 && make
RUN apt update
RUN apt upgrade -y
RUN apt dist-upgrade -y
RUN apt install gdb -y

CMD /usr/app/Test/build/Debug/Test
