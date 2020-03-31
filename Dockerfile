FROM suilteam/suil-prod:unstable
MAINTAINER "Carter Mbotho <carter@suilteam.com>"
# User to use for building
RUN adduser -s /bin/sh -h /home/semausu/ -D admin
# copy examples build folder
COPY artifacts/ /usr/
RUN ln -s /usr/share/gateway/ /home/semausu/gateway

WORKDIR /home/semausu