var PORT = 8080;

var dgram = require('dgram');
var server = dgram.createSocket('udp4');

server.on('listening', function() {
    var address = server.address();
    console.log('UDP Server listening on ' + address.address + ':' + address.port);
});

server.on('message', function(message, remote) {
    if (message == "GET_IP") {
        require('dns').lookup(require('os').hostname(), function (err, addr, fam) {
            console.log('IP address: ' + addr);
            var ipAddr = {addr: addr};
            server.send(JSON.stringify(ipAddr), remote.port, remote.address);
        })
    }
});

server.bind(PORT);