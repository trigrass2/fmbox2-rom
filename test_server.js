var http = require("http"),
		url  = require("url"),
		path = require("path"),
		fs   = require("fs");

console.log("starts");

http.createServer(function (req, res) {
	res.setHeader("Set-Cookie", "k1=v1; k2=v2;");
	res.setHeader("Set-Cookie", "k3=v3; k4=v4;");
	res.setHeader("Transfer-Encoding", "chunked");
	res.writeHead(200, {"Content-Type": "text/html"});

	var p = url.parse(req.url);
	var data = "";
	req.on("data", function (chunk) {
		data += chunk;
	});
	req.on("end", function () {
		console.log("GET ", p.pathname);
		console.log("  ", data);
		switch (p.pathname) {
		case "/echo":
			res.end(data);
			break;
		}
	});
}).listen(1989, "localhost");

