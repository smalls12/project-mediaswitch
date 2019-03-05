# project mediaswitch

There are 3 components to this project

* project-control: Call control server
* project-sip: A SIP server
* project-rtp: An RTP server

Between the 3 sub projects, events re communicated via a HTTP event mechanism. i.e. if a new SIP call is generated by a client (which sends a SIP INVITE) then project-sip will pass a HTTP POST request back to the control server. Control server  will then communicate back instructions to both the SIP server and the RTP server.

The design of this project is designed to work with cloud services so that workloads can be scaled up and down appropriately.

# Interfaces

All three projects are designed to either run on the same physical server or separate servers. This way load balancing between servers can be achieved. Multiple RTP servers can then be run to handle large volumes of transcoding for every SIP and control server.

All services communicate with each other via HTTP. The following section defines the interfaces. In this section all of the examples use curl to get or post data.

## project-control 

### POST http://control/invite

Similar to the sip server invite this call will originate a new call - but it will go through the call processing first (compared to the SIP interface which will blindly call the SIP endpoint).



### POST http://control/reg

Notify the control server of a SIP registration.

Example:
curl -X POST --data-raw '{ "domain": "bling.babblevoice.com", "user": "1000" }' -H "Content-Type:application/json" http://control/reg

### DELETE http://control/reg

Notify the control server of a SIP deregistration.

Example:
curl -X DELETE --data-raw '{ "domain": "bling.babblevoice.com", "user": "1000" }' -H "Content-Type:application/json" http://control/reg

## project-sip

### POST http://sip/dir

This interface is used to add directory information to the SIP server.

Example:
curl -X POST --data-raw '[{ "domain": "bling.babblevoice.com", "control": { "host": "127.0.0.1", "port": 9001 }, "users": [ { "username": "1003", "secret": "1123654789"}]}]' -H "Content-Type:application/json" http://127.0.0.1/dir

* domain: the name of the sip domain
* control: a structure of host and port of the control server responsible for this domain
* users: an array of user structures containing username and secret


### GET http://sip/dir/bling.babblevoice.com

Returns JSON listing this domains entry in the directory.


### GET http://sip/reg

Returns the number of registered clients.

### POST http://sip/reg

TODO - register (outbound) with a SIP service.

### POST http://sip/dialog/invite

Originate a new call.

curl -X POST --data-raw '[{ "domain": "bling.babblevoice.com", "to: "", "from": "", "maxforwards": 70, "callerid": { "number": "123", "name": "123", "private": false }, "control": { "host": "127.0.0.1", "port": 9001 } }]' -H "Content-Type:application/json" http://127.0.0.1/invite

The control option is optional. If it is in this is the server which will receive updates regarding the call flow. If not, the default one listed in the "to" field will be used. If not this, then no updates will be sent.

### POST http://sip/dialog/ring

Example:
curl -X  POST --data-raw '{ "callid": "<callid>", "alertinfo": "somealertinfo" }' -H "Content-Type:application/json" http://sip/dir

If the call is not in a ringing or answered state it will send a 180 ringing along with alert info if sent.


### GET http://sip/dialog

## project-rtp

### POST http://rtp/

Posting a blank document will create a new channel. 

Example:
curl -X  POST --data-raw '{}' -H "Content-Type:application/json" http://rtp/

The server will return a JSON document. Including stats regarding the workload of the server so that the control server can make decisions based on workload as well as routing.

# RFCs used in this project

* RFC 2616: Hypertext Transfer Protocol -- HTTP/1.1
* RFC 2617: HTTP Authentication: Basic and Digest Access Authentication
* RFC 3261: SIP: Session Initiation Protocol
* RFC 4566: SDP: Session Description Protocol

# Testing

The SIP server can be built with the test flag:

make test

Which will build siptest which will run through a bunch of internal testing
to try and ensure nothing is broken.

In the folder testfiles there are also other test files.

The defult ports for the server are 8080 for the web server and 5060 for the SIP server.

## Registry

registerclient.xml & .csv.

Which are config files to be used with sipp which can test various scenarios.

sipp 127.0.0.1 -sf registerclient.xml -inf registerclient.csv -m 1 -l 1 -trace_msg -trace_err

or without the logging files.

sipp 127.0.0.1 -sf registerclient.xml -inf registerclient.csv -m 1 -l 1

To upload test data to the sip server use

## Invite

sipp 127.0.0.1 -sf uaclateoffer.xml -m 1 -l 1

