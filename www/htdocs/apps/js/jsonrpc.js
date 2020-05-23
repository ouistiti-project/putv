function loadSymbol(functionName, context /*, args */) {
  var namespaces = functionName.split(".");
  var func = namespaces.pop();
  for(var i = 0; i < namespaces.length; i++) {
    context = context[namespaces[i]];
  }
  return context[func];
}

class JsonRPC{
	constructor(url)
	{
		this.prototype = Object.create(EventTarget.prototype);
		this.urlsocket = "";
		if (url != undefined && url.match(/^ws[s]:\/\//))
			this.urlsocket = url;
		else
		{
			if (location.protocol === "http:")
				this.urlsocket = "ws://";
			else if (location.protocol === "https:")
				this.urlsocket = "wss://";
			this.urlsocket += location.hostname;
			this.urlsocket += ":"+location.port;
			this.urlsocket += "/"+url;
		}
		this.id = 0;
		this.timer = 0;
		this.wsready = false;
		this.cnt = 0;
		this.string = "";
		this.cmds = new Array();
	}

	connect()
	{
		if (this.timer)
			clearTimeout(this.timer);
		this.websocket=new WebSocket(this.urlsocket);

		this.websocket.onopen = function(evt) {
			this.wsready = true;
			if (typeof(this.onopen) == "function")
				this.onopen.call(this);
		}.bind(this);
		this.websocket.onmessage = this.receive.bind(this);
		this.websocket.onerror = function(evt)
		{
			this.string = "";
			this.cnt = 0;
		}.bind(this);
		this.websocket.onclose = function(evt)
		{
			console.log("socket close: "+evt.toString());
			this.reconnect.bind(this);
			if (typeof(this.onclose) == "function")
				this.onclose.call(this);
		}.bind(this);
		this.runRPC = function(string)
		{
			var data;
			try {
				data = JSON.parse(string);
			}
			catch(error) {
				console.log("recv: "+string);
				console.log(error);
				return;
			}
			if (data.id != undefined)
			{
				if (this.cmds[data.id] != undefined)
				{
					data.method = this.cmds[data.id].method;
					data.request = this.cmds[data.id];
				}
				else
					console.log("method id  "+data.id + "not found");
				this.cmds[data.id] = undefined;
			}
			if (data.error)
			{
				console.log("response error  "+data.error);
				if (typeof(this.onerror) == "function")
					this.onerror.call(this, data.error, data.request);
			}
			else if (data.method)
			{
				if (typeof(this.respond) == "function")
					this.respond.call(this, data.result);
				this.respond = undefined;
				var func = loadSymbol(data.method, this);
				if (data.result && typeof(func) == "function")
				{
					console.log("response "+data.method);
					func.call(this,data.result);
				}
				else if (data.params && typeof(func) == "function")
				{
					console.log("notification "+data.method);
					func.call(this,data.params);
				}
				else
					console.log("method "+data.method + "not connected to "+typeof(func));
			}
			if (typeof(this.onmessage) == "function")
				this.onmessage.call(this, data);
		}.bind(this);
	}

	receive_old(evt)
	{
		var start = 0;
		if (this.cnt > 0)
			start = 1;
		//console.log("receive 1"+ this.cnt +" : "+this.string);
		//console.log("receive 2"+ this.cnt +" : "+evt.data);
		console.log("receive : "+evt.data);
		var i;
		for (i = 0; i < evt.data.length; i++)
		{
			if (evt.data[i] == '\'')
			{
				this.string += '\\\\';
			}
			if (start == 1)
				this.string += evt.data[i];
			if (evt.data[i] == '{')
			{
				if (start == 0)
				{
					this.string += evt.data[i];
					start = 1;
				}
				this.cnt++;
			}
			if (evt.data[i] == '}')
			{
				this.cnt--;
				if (start == 1 && this.cnt == 0)
				{
					start = 0;
					var string = this.string;
					this.string = "";
					//console.log("recv "+this.cnt+" : "+evt.data);
					this.runRPC(string);
				}
			}
		}
	}
	receive(evt)
	{
		//console.log("receive : "+evt.data);
		this.runRPC(evt.data);
	}

	reconnect()
	{
		if (typeof(this.onclose) == "function")
			this.onclose.call(this);
		this.wsready = false;
		this.timer = setTimeout(this.connect.bind(this), 3000);
	}

	close()
	{
		this.wsready = false;
		if (this.timer)
			clearTimeout(this.timer);
		this.websocket.onclose = function ()
		{
			if (typeof(this.onclose) == "function")
				this.onclose.call(this);
		}.bind(this);
		this.websocket.close();
	}

	send(method, params, respond)
	{
		console.log("send "+method);
		this.respond = respond;
		var request = new Object();
		request.jsonrpc = "2.0";
		request.method = method.toString();
		var paramsstr;
		if (typeof(params) == "object")
			request.params = params;
		else
		{
			try {
				request.params = JSON.parse(params);
			}
			catch(error) {
				console.log("params: "+params);
				console.log(error);
				return;
			}
		}
		request.id = this.id;

		if (this.wsready)
		{
			var msg = JSON.stringify(request);
			this.cmds[this.id] = request;
			//console.log("send :"+msg);
			this.websocket.send(msg);
			this.id++;
		}
	}
}
