var PORT = 8080;
var HOST = '255.255.255.255';

var dgram = require('dgram');
var os = require('os');
var message = new Buffer('Hehe!');

var client = dgram.createSocket('udp4');
client.bind(function () {
    client.setBroadcast(true);
});

// Get IPAddress
// var networkInterfaces = os.networkInterfaces();
// if (networkInterfaces.wlan0) {
//     var wlan0 = networkInterfaces.wlan0.find(o => o.family == "IPv4");
//     if (wlan0) {
//         var netmask = wlan0.netmask;
//     }
// }
// require('dns').lookup(require('os').hostname(), function (err, addr, fam) {
//     console.log(addr);
// })

client.send(message, 0, message.length, PORT, HOST, function(err, bytes) {
    if (err) throw err;
    console.log('UDP message sent to ' + HOST +':'+ PORT);
    client.close();
});