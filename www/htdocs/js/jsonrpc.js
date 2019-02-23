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
			console.log("error: "+evt.error);
			this.string = "";
			this.cnt = 0;
		}.bind(this);
		this.websocket.onclose = this.reconnect.bind(this);
		this.runRPC = function(string)
		{
			var data;
			try {
				data = JSON.parse(string);
			}
			catch(error) {
				alert(error);
			}
			if (data.id != undefined)
			{
				if (this.cmds[data.id] != undefined)
				{
					data.method = this.cmds[data.id].method;
					data.request = this.cmds[data.id];
				}
				this.cmds[data.id] = undefined;
			}
			if (data.error)
			{
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
					func.call(this,data.result);
				if (data.params && typeof(func) == "function")
					func.call(this,data.params);
			}
			if (typeof(this.onmessage) == "function")
				this.onmessage.call(this, data);
		}.bind(this);
	}

	receive(evt)
	{
		var start = 0;
		if (this.cnt > 0)
			start = 1;
		console.log("receive 1"+ this.cnt +" : "+this.string);
		console.log("receive 2"+ this.cnt +" : "+evt.data);
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
					console.log("recv "+this.cnt+" : "+evt.data);
					this.runRPC(string);
				}
			}
		}
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
		this.respond = respond;
		var request = new Object();
		request.jsonrpc = "2.0";
		request.method = method.toString();
		var paramsstr;
		if (typeof(params) == "object")
			request.params = params;
		else
			request.params = JSON.parse(params);
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
