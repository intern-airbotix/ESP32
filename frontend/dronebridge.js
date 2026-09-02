const ROOT_URL = window.location.href       // for production code
// const ROOT_URL = "http://localhost:3000/"   // for testing with local json server
let conn_status = 0;		// connection status to the ESP32
let old_conn_status = 0;	// connection status before last update of UI to know when it changed
let ever_connected = false;	// true after first successful connection (used to gate initial settings load)
let disconnect_time = 0;	// timestamp of when we went offline (ms); 0 = currently online
let serial_via_JTAG = 0;	// set to 1 if ESP32 is using the USB interface as serial interface for data and not using the UART. If 0 we set UART config to invisible for the user.
let last_byte_count = 0;
let last_timestamp_byte_count = 0;
let esp_chip_model = 0;		// according to get_esp_chip_model_str()
let recv_ser_bytes = 0;		// Total bytes received from serial interface
let serial_dec_mav_msgs = 0;	// Total MAVLink messages decoded from serial interface
let set_telem_proto = null;		// Telemetry protocol received by the ESP32
let wifi_5ghz_supported = 0;	// 1 when api/system/info reports a chip with 5 GHz support (ESP32-C5), else 0

function change_radio_dis_arm_visibility() {
	// we only support this feature when MAVLink or LTM are set AND when a standard Wi-Fi mode or BLE is enabled
	let radio_dis_onarm_div = document.getElementById("radio_dis_onarm_div")
	if ((document.getElementById("esp32_mode").value > "2" &&  document.getElementById("esp32_mode").value < "6") || document.getElementById("proto").value === "5") {
		radio_dis_onarm_div.style.display = "none";
	} else {
		radio_dis_onarm_div.style.display = "block";
	}
}

function change_ap_ip_visibility() {
	const mode = document.getElementById("esp32_mode").value;
	const el = {
		ap_ip_div:            document.getElementById("ap_ip_div"),
		ap_channel_div:       document.getElementById("ap_channel_div"),
		lr_disclaimer_div:    document.getElementById("esp-lr-ap-disclaimer"),
		ble_disclaimer_div:   document.getElementById("ble_disclaimer_div"),
		sta_section_div:      document.getElementById("sta_section_div"),
		ap_section_div:       document.getElementById("ap_section_div"),
		wifi_en_gn_div:       document.getElementById("wifi_en_gn_div"),
		static_ip_config_div: document.getElementById("static_ip_config_div"),
	};

	// hide everything, then selectively show per mode
	Object.values(el).forEach(e => { e.style.display = "none"; });

	if (mode === "2") {
		// Wi-Fi STA + AP fallback: show all credential sections and all AP config
		el.sta_section_div.style.display      = "block";
		el.ap_section_div.style.display       = "block";
		el.ap_ip_div.style.display            = "block";
		el.ap_channel_div.style.display       = "block";
		el.wifi_en_gn_div.style.display       = "block";
		el.static_ip_config_div.style.display = "block";
	} else if (mode === "3") {
		// AP LR: AP credentials + channel + IP only
		el.ap_section_div.style.display    = "block";
		el.ap_ip_div.style.display         = "block";
		el.ap_channel_div.style.display    = "block";
		el.lr_disclaimer_div.style.display = "block";
	} else if (mode === "4" || mode === "5") {
		// ESP-NOW: no SSID/pass needed
		el.lr_disclaimer_div.style.display = "block";
	} else if (mode === "6") {
		// BLE: the configuration access point keeps running next to Bluetooth, so its SSID/pass, IP and channel apply
		el.ap_section_div.style.display     = "block";
		el.ap_ip_div.style.display          = "block";
		el.ap_channel_div.style.display     = "block";
		el.ble_disclaimer_div.style.display = "block";
	}
	change_radio_dis_arm_visibility();
	change_wifi_band_visibility();	// may swap the 2.4 GHz channel row for the 5 GHz one
}

/**
 * Checks whether change_ap_ip_visibility() shows the 2.4 GHz "ap_channel_div" row for the given mode.
 * Those are the modes of this fork that host an access point with a user configurable 2.4 GHz channel:
 * Wi-Fi client with AP fallback (2), Wi-Fi access point LR mode (3) and Bluetooth LE with its configuration AP (6).
 * Bluetooth LE (6) also hosts a configuration access point, but change_ap_ip_visibility() hides its
 * channel row - it is only shown by change_wifi_band_visibility() on chips with a 5 GHz radio, see
 * mode_band_configurable().
 * @param mode Value of the esp32_mode select as string
 * @returns {boolean} true when change_ap_ip_visibility() shows the access point channel row
 */
function mode_hosts_ap(mode) {
	return mode === "2" || mode === "3" || mode === "6";
}

/**
 * Checks whether the firmware honours the wifi_band parameter in the given ESP32 mode.
 * The band is applied to the AP fallback of the Wi-Fi client mode (2) and to the configuration access
 * point that runs in parallel to Bluetooth LE (6). In both cases a stored 5 GHz band makes the access
 * point unreachable for 2.4 GHz only devices, so the setting must be visible and editable there.
 * @param mode Value of the esp32_mode select as string
 * @returns {boolean} true when the band select must be editable in this mode
 */
function mode_band_configurable(mode) {
	return mode === "2" || mode === "6";
}

/**
 * Checks whether the access point of the given mode is the only Wi-Fi interface, i.e. there is no
 * station interface that could make use of the "auto" band setting.
 * @param mode Value of the esp32_mode select as string
 * @returns {boolean} true when the mode has no Wi-Fi station interface
 */
function mode_is_ap_only(mode) {
	return mode === "6";
}

/**
 * Checks whether the given ESP32 mode is tied to the 2.4 GHz band by the radio protocol it uses.
 * Wi-Fi long range mode and ESP-NOW are 2.4 GHz only, no matter what the chip supports.
 * @param mode Value of the esp32_mode select as string
 * @returns {boolean} true when the mode can only ever use 2.4 GHz
 */
function mode_is_24ghz_only(mode) {
	return mode === "3" || mode === "4" || mode === "5";
}

/**
 * Restricts a Wi-Fi band value to the options offered by the GUI (0 = 2.4 GHz, 1 = 5 GHz, 2 = auto).
 * Anything else - e.g. a value from a newer firmware - falls back to 2.4 GHz.
 * @param value Wi-Fi band value as string
 * @returns {string} A band value that is guaranteed to exist as an option of the wifi_band select
 */
function sanitize_wifi_band(value) {
	return (value === "0" || value === "1" || value === "2") ? value : "0";
}

/**
 * Locks/unlocks a select so the user cannot change it while its value is still submitted with the form.
 * The disabled attribute is deliberately not used: a disabled control would be dropped by the browser
 * on a normal form submit and is easy to overlook when the form is serialised (see toJSONString()).
 * @param select The select element to lock or unlock
 * @param locked true to lock the element, false to make it editable again
 */
function set_select_locked(select, locked) {
	if (locked) {
		select.classList.add("locked_input");
		select.setAttribute("tabindex", "-1");
		select.setAttribute("aria-disabled", "true");
	} else {
		select.classList.remove("locked_input");
		select.removeAttribute("tabindex");
		select.removeAttribute("aria-disabled");
	}
}

/**
 * Builds the warning shown when an access point is configured for the 5 GHz band. It names the channel
 * currently selected in the wifi_chan_5g select so the user knows what to look for and that a 2.4 GHz
 * only device (e.g. an older phone) will not see the access point.
 * @param ap_name How to call the access point in the hint, e.g. "fallback access point"
 * @returns {string} The hint text
 */
function five_ghz_ap_hint(ap_name) {
	const chan_sel = document.getElementById("wifi_chan_5g");
	const chan = (chan_sel != null && chan_sel.value !== "") ? chan_sel.value : "?";
	return "The " + ap_name + " will use 5 GHz channel " + chan + "; use a 5 GHz-capable device to reach it.";
}

/**
 * Shows/hides & locks the Wi-Fi band selection and shows the channel selection that matches the band.
 * Called whenever the ESP32 mode, the band or the 5 GHz channel changes, once the system info is known
 * and after the settings have been loaded. Rules:
 *  - chip without 5 GHz support (or firmware not reporting "wifi_5ghz"): no band row at all, GUI behaves
 *    exactly like before
 *  - Wi-Fi client with AP fallback (2) & Bluetooth LE (6): band selectable, the channel select follows
 *    the band. "Auto" needs a station interface and is therefore not selectable in BLE mode, where the
 *    access point is the only Wi-Fi interface
 *  - AP LR (3) & ESP-NOW (4, 5): band row visible but locked, because the firmware always uses 2.4 GHz
 *    in those modes. The stored value is left untouched so it is not overwritten when the form is saved
 *  - an unknown mode: band row hidden
 * The band select is never given the disabled attribute and its value is never changed by this function:
 * the settings POST contains every select of the form, so writing to it would destroy the stored band.
 */
function change_wifi_band_visibility() {
	const band_div = document.getElementById("wifi_band_div");
	const band_sel = document.getElementById("wifi_band");
	const auto_opt = document.getElementById("wifi_band_auto_opt");
	const hint_div = document.getElementById("wifi_band_hint");
	const chan_5g_div = document.getElementById("wifi_chan_5g_div");
	const ap_channel_div = document.getElementById("ap_channel_div");
	const mode = document.getElementById("esp32_mode").value;
	if (band_div == null || band_sel == null || auto_opt == null || hint_div == null || chan_5g_div == null ||
		ap_channel_div == null) {
		return;	// markup not available - nothing to do
	}
	const band_configurable = mode_band_configurable(mode);
	const locked = mode_is_24ghz_only(mode);
	if (wifi_5ghz_supported !== 1 || (!band_configurable && !locked)) {
		// 2.4 GHz only hardware/firmware or a mode without any Wi-Fi band (unknown mode)
		band_div.style.display = "none";
		hint_div.style.display = "none";
		chan_5g_div.style.display = "none";
		set_select_locked(band_sel, false);
		auto_opt.disabled = false;
		if (mode_hosts_ap(mode)) {
			ap_channel_div.style.display = "block";
		}
		return;
	}
	band_div.style.display = "block";
	// auto (2.4 + 5 GHz) is a station feature - it cannot be used when the AP is the only interface
	auto_opt.disabled = mode_is_ap_only(mode);
	set_select_locked(band_sel, locked);
	const band = band_sel.value;
	let hint = "";
	if (locked) {
		hint = "2.4 GHz only in this mode; the stored band is kept for other modes.";
	} else if (mode === "6") {
		hint = "Applies to the configuration access point that runs alongside Bluetooth LE.";
		if (band === "1") {
			hint += " " + five_ghz_ap_hint("configuration access point");
		} else if (band === "2") {
			hint += " Auto is client mode only - the access point will use 2.4 GHz.";
		}
	} else if (mode === "2") {
		if (band === "1") {
			hint = five_ghz_ap_hint("fallback access point");
		} else if (band === "2") {
			hint = "Auto applies to the client connection - the AP fallback uses the 2.4 GHz channel.";
		}
	}
	hint_div.textContent = hint;
	hint_div.style.display = (hint === "") ? "none" : "block";
	// only one of the two channel selects is ever visible
	const use_5g_chan = (band === "1" && band_configurable);
	chan_5g_div.style.display = use_5g_chan ? "block" : "none";
	if (use_5g_chan) {
		ap_channel_div.style.display = "none";
	} else if (mode_hosts_ap(mode) || band_configurable) {
		ap_channel_div.style.display = "block";
	}
}

function change_msp_ltm_visibility(){
	let msp_ltm_div = document.getElementById("msp_ltm_div");
	let trans_pack_size_div = document.getElementById("trans_pack_size_div");
	let rep_rssi_dbm_div = document.getElementById("rep_rssi_dbm_div");
	let telem_proto = document.getElementById("proto");
	if (telem_proto.value === "1") {
		msp_ltm_div.style.display = "block";
		trans_pack_size_div.style.display = "none";

	} else {
		msp_ltm_div.style.display = "none";
		trans_pack_size_div.style.display = "block";
	}
	if (telem_proto.value === "4") {
		rep_rssi_dbm_div.style.display = "block";
	} else {
		rep_rssi_dbm_div.style.display = "none";
	}
	change_radio_dis_arm_visibility();
}

function change_uart_visibility() {
	let tx_rx_div = document.getElementById("tx_rx_div");
	let rts_cts_div = document.getElementById("rts_cts_div");
	let rts_thresh_div = document.getElementById("rts_thresh_div");
	let baud_div = document.getElementById("baud_div");
	if (serial_via_JTAG === 0) {
		rts_cts_div.style.display = "block";
		tx_rx_div.style.display = "block";
		rts_thresh_div.style.display = "block";
		baud_div.style.display = "block";
	} else {
		rts_cts_div.style.display = "none";
		tx_rx_div.style.display = "none";
		rts_thresh_div.style.display = "none";
		baud_div.style.display = "none";
	}
}

function flow_control_check() {
	let gpio_rts = document.getElementById("gpio_rts");
	let gpio_cts = document.getElementById("gpio_cts");
	if (isNaN(gpio_rts.value) || isNaN(gpio_cts.value) || gpio_cts.value === '' || gpio_rts.value === '' || gpio_rts.value === gpio_cts.value) {
		show_toast("UART flow control disabled.")
	} else {
		show_toast("UART flow control enabled. Make sure RTS & CTS pins are connected!");
	}
}

/**
 * Convert a form into a JSON string
 * @param form The HTML form to convert
 * @returns {string} JSON formatted string
 */
function toJSONString(form) {
	let obj = {}
	let elements = form.querySelectorAll("input, select")
	for (let i = 0; i < elements.length; ++i) {
		let element = elements[i]
		let name = element.name;
		let value = element.value;
		// parse numbers as numbers except for SSID, password and IP string fields.
		// udp_client_ip must always be sent as a string: an empty field would otherwise become
		// Number("") = 0 -> parseInt -> NaN -> null in JSON and the firmware would silently keep the old IP.
		if (!isNaN(Number(value)) && name !== "ssid" && name !== "wifi_pass" && name !== "sta_ssid" && name !== "sta_pass" && name !== "udp_client_ip") {
			if (name) {
				obj[name] = parseInt(value)
			}
		} else {
			if (name) {
				if (element.type === "checkbox") {
					// convert checked/not checked to 1 & 0 as value
					obj[name] = element.checked ? 1 : 0;
				} else {
					// just get the value specified by the input/select
					obj[name] = value
				}
			}
		}
	}
	return JSON.stringify(obj)
}

/**
 * Request data from the ESP to display in the GUI
 * @param api_path API path/request path
 * @returns {Promise<any>}
 */
async function get_json(api_path) {
	let req_url = ROOT_URL + api_path;

	const controller = new AbortController()
	// Set a timeout limit for the request using `setTimeout`. If the body
	// of this timeout is reached before the request is completed, it will
	// be cancelled.

	const timeout = setTimeout(() => {
		controller.abort()
	}, 1000)
	const response = await fetch(req_url, {
		signal: controller.signal
	});
	if (!response.ok) {
		const message = `An error has occured: ${response.status}`;
		conn_status = 0
		throw new Error(message);
	}
	return await response.json();
}

/**
 * Create a response with JSON data attached
 * @param api_path API URL path
 * @param json_data JSON body data to send
 * @returns {Promise<any>}
 */
async function send_json(api_path, json_data = undefined) {
	let post_url = ROOT_URL + api_path;
	const response = await fetch(post_url, {
		method: 'POST',
		headers: {
			'Accept': 'application/json',
			'Content-Type': 'application/json',
			"charset": 'utf-8'
		},
		body: json_data
	});
	if (!response.ok) {
		conn_status = 0
		const message = `An error has occured: ${response.status}`;
		throw new Error(message);
	}
	return await response.json();
}

function get_esp_chip_model_str(esp_model_index) {
	switch (esp_model_index) {
		default:
		case 0:
			return "unknown/unsupported ESP32 chip";
		case 1:
			return "ESP32";
		case 2:
			return "ESP32-S2";
		case 9:
			return "ESP32-S3";
		case 5:
			return "ESP32-C3";
		case 13:
			return "ESP32-C6";
		case 12:
			return "ESP32-C2";
		case 23:
			return "ESP32-C5";
	}
}

function get_system_info() {
	get_json("api/system/info").then(json_data => {
		console.log("Received settings: " + json_data)
		document.getElementById("about").innerHTML = "DroneBridge for ESP32 v" + json_data["major_version"] +
			"." + json_data["minor_version"] + "." + json_data["patch_version"] + " ("+json_data["maturity_version"]+")" +
			" - esp-idf " + json_data["idf_version"] + " - " + get_esp_chip_model_str(json_data["esp_chip_model"])
		document.getElementById("esp_mac").innerHTML = json_data["esp_mac"]
		serial_via_JTAG = json_data["serial_via_JTAG"];
		// set external antenna option visible based on info if RF switch is available on the board
		if (parseInt(json_data["has_rf_switch"]) === 1) {
			document.getElementById("ant_use_ext_div").style.display = "block";
		} else {
			document.getElementById("ant_use_ext_div").style.display = "none";
		}
		// only offer the Wi-Fi band selection on chips that support 5 GHz (ESP32-C5). Firmware that does
		// not know the "wifi_5ghz" key at all keeps the 2.4 GHz only GUI.
		wifi_5ghz_supported = (parseInt(json_data["wifi_5ghz"]) === 1) ? 1 : 0;
		change_wifi_band_visibility();
	}).catch(error => {
		conn_status = 0
		error.message;
		return -1;
	});
	return 0;
}

function update_conn_status() {
	if (conn_status)
		document.getElementById("web_conn_status").innerHTML = "<span class=\"dot_green\"></span> connected to ESP32"
	else {
		document.getElementById("web_conn_status").innerHTML = "<span class=\"dot_red\"></span> disconnected from ESP32"
		document.getElementById("current_client_ip").innerHTML = ""
		document.getElementById("radio_status").textContent = ""
	}
	if (conn_status !== old_conn_status) {
		if (conn_status === 1) {
			// Just (re)connected
			get_system_info();
			// Refresh settings on first connect OR after a long outage (device rebooted).
			// Skip on brief flickers (<3 s) so a 1-second AbortController timeout doesn't
			// clobber form fields the user is actively editing.
			const long_outage = disconnect_time > 0 && (Date.now() - disconnect_time) > 3000;
			if (!ever_connected || long_outage) {
				get_settings();
				setTimeout(change_msp_ltm_visibility, 500);
				setTimeout(change_ap_ip_visibility, 500);
				setTimeout(change_uart_visibility, 500);
			}
			ever_connected = true;
			disconnect_time = 0;
		} else {
			// Just went offline — record when
			disconnect_time = Date.now();
		}
	}
	old_conn_status = conn_status
}

/**
 * Display the band & channel the radio is currently using, e.g. "Radio: 5 GHz, channel 36".
 * Firmware that does not report "wifi_band_mode"/"wifi_channel" leaves the line empty.
 * @param json_data Parsed JSON response of api/system/stats
 */
function update_radio_status(json_data) {
	let radio_div = document.getElementById("radio_status");
	if (radio_div == null) {
		return;
	}
	if (!('wifi_band_mode' in json_data) && !('wifi_channel' in json_data)) {
		radio_div.textContent = "";
		return;
	}
	let band_str = "";
	switch (parseInt(json_data["wifi_band_mode"])) {
		case 1:
			band_str = "2.4 GHz";
			break;
		case 2:
			band_str = "5 GHz";
			break;
		case 3:
			band_str = "auto (2.4 + 5 GHz)";
			break;
		default:	// 0 or missing: the ESP32 does not know the band
			break;
	}
	let channel = parseInt(json_data["wifi_channel"]);
	let parts = [];
	if (band_str !== "") {
		parts.push(band_str);
	}
	if (!isNaN(channel) && channel > 0) {
		parts.push("channel " + channel);
	}
	radio_div.textContent = "Radio: " + (parts.length > 0 ? parts.join(", ") : "unknown");
}

/**
 * Get connection status information and display it in the GUI
 */
function get_stats() {
	get_json("api/system/stats").then(json_data => {
		conn_status = 1
		let d = new Date();
		recv_ser_bytes = parseInt(json_data["read_bytes"]);
		serial_dec_mav_msgs = parseInt(json_data["serial_dec_mav_msgs"]);
		let bytes_per_second = 0;
		let current_time = d.getTime();
		if (last_byte_count > 0 && last_timestamp_byte_count > 0 && !isNaN(recv_ser_bytes)) {
			bytes_per_second = (recv_ser_bytes - last_byte_count) / ((current_time - last_timestamp_byte_count) / 1000);
		}
		last_timestamp_byte_count = current_time;
		if (!isNaN(recv_ser_bytes) && recv_ser_bytes > 1000000) {
			document.getElementById("read_bytes").innerHTML = (recv_ser_bytes / 1000000).toFixed(3) + " MB (" + ((bytes_per_second*8)/1000).toFixed(2) + " kbit/s)"
		} else if (!isNaN(recv_ser_bytes) && recv_ser_bytes > 1000) {
			document.getElementById("read_bytes").innerHTML = (recv_ser_bytes / 1000).toFixed(2) + " kB (" + ((bytes_per_second*8)/1000).toFixed(2) + " kbit/s)"
		} else if (!isNaN(recv_ser_bytes)) {
			document.getElementById("read_bytes").innerHTML = recv_ser_bytes + " bytes (" + Math.round(bytes_per_second) + " byte/s)"
		}
		last_byte_count = recv_ser_bytes;

		let tcp_clients = parseInt(json_data["tcp_connected"])
		if (!isNaN(tcp_clients) && tcp_clients === 1) {
			document.getElementById("tcp_connected").innerHTML = tcp_clients + " client"
		} else if (!isNaN(tcp_clients)) {
			document.getElementById("tcp_connected").innerHTML = tcp_clients + " clients"
		}
		// UDP clients for tooltip
		let udp_clients_string = ""
		if (json_data.hasOwnProperty("udp_clients")) {
			let udp_conn_jsonarray = json_data["udp_clients"];
			for (let i = 0; i < udp_conn_jsonarray.length; i++) {
				udp_clients_string = udp_clients_string + udp_conn_jsonarray[i];
				if ((i + 1) !== udp_conn_jsonarray.length) {
					udp_clients_string = udp_clients_string + "<br>";
				}
			}
			if (udp_conn_jsonarray.length === 0) {
				udp_clients_string = "-";
			} else {
				document.getElementById("tooltip_udp_clients").innerHTML = udp_clients_string;
			}
		}

		let udp_clients = parseInt(json_data["udp_connected"])
		if (!isNaN(udp_clients) && udp_clients === 1) {
			document.getElementById("udp_connected").innerHTML = "<span class=\"tooltiptext\" id=\"tooltip_udp_clients\">"+udp_clients_string+"</span>" + udp_clients + " client"
		} else if (!isNaN(udp_clients)) {
			document.getElementById("udp_connected").innerHTML = "<span class=\"tooltiptext\" id=\"tooltip_udp_clients\">"+udp_clients_string+"</span>" + udp_clients + " clients"
		}

		if ('esp_rssi' in json_data) {
			let rssi = parseInt(json_data["esp_rssi"])
			if (!isNaN(rssi) && rssi < 0) {
				document.getElementById("current_client_ip").innerHTML = "IP Address: " + json_data["current_client_ip"] + "<br />Signal Strength: " + rssi + "dBm"
			} else if (!isNaN(rssi)) {
				document.getElementById("current_client_ip").innerHTML = "IP Address: " + json_data["current_client_ip"]
			}
		} else if ('connected_sta' in json_data) {
			let a = ""
			json_data["connected_sta"].forEach((item) => {
				a = a + "Client: " + item.sta_mac + " Signal Strength: " + item.sta_rssi + "dBm<br />"
			});
			document.getElementById("current_client_ip").innerHTML = a
		}

		update_radio_status(json_data);

	}).catch(error => {
		conn_status = 0
		error.message;
	});
}

/**
 * Get settings from ESP and display them in the GUI. JSON objects have to match the element ids
 *  returns 0 on success and -1 on failure
 */
function get_settings() {
	get_json("api/settings").then(json_data => {
		console.log("Received settings: " + json_data)
		conn_status = 1
		for (const key in json_data) {
			if (json_data.hasOwnProperty(key)) {
				let elem = document.getElementById(key)
				if (elem != null) {
					if (elem.type === "checkbox") {
						// translate 1 & 0 to checked and not checked
						elem.checked = json_data[key] === 1;
					} else {
						elem.value = json_data[key] + ""
					}
				}
			}
		}
		set_telem_proto = document.getElementById("proto").value;
		// A band value this GUI does not offer (e.g. from a newer firmware) leaves the select without a
		// selected option and would be sent back as null on the next save - fall back to 2.4 GHz instead.
		let band_sel = document.getElementById("wifi_band");
		if (band_sel != null) {
			band_sel.value = sanitize_wifi_band(band_sel.value);
		}
		let chan5_sel = document.getElementById("wifi_chan_5g");
		if (chan5_sel != null && chan5_sel.value === "") {
			chan5_sel.value = "36";  // value not offered by this GUI (e.g. a DFS channel) - fall back to the default
		}
		change_wifi_band_visibility();
	}).catch(error => {
		conn_status = 0
		error.message;
		show_toast(error.message);
		return -1;
	});
	change_ap_ip_visibility();
	change_msp_ltm_visibility();
	return 0;
}

function add_new_udp_client() {
	let ip = prompt("Please enter the IP address of the UDP receiver", "192.168.2.X");
	if (ip == null) {
		show_toast("Operation cancelled by user."); 
		return;
	}
	let port = prompt("Please enter the port number of the UDP receiver", "14550");
	if (port == null) {
		show_toast("Operation cancelled by user.")
		return;
	}
	port = parseInt(port);
	let save_to_nvm = confirm("Save this UDP client to the permanent storage so it will be auto added after reboot/reset?\nYou can only save one UDP client to the memory. The old ones will be overwritten.\nSelect no if you only want to add this client for this session.");
	const ippattern = /^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$/;

	if (ip != null && port != null && ippattern.test(ip)) {
		let myjson = {
			udp_client_ip: ip,
			udp_client_port: port,
			save: save_to_nvm
		};
		send_json("api/settings/clients/udp", JSON.stringify(myjson)).then(send_response => {
			console.log(send_response);
			conn_status = 1
			show_toast(send_response["msg"])
		}).catch(error => {
			show_toast(error.message);
		});
	} else {
		show_toast("Error: Enter valid IP and port!")
	}
}

async function clear_udp_clients() {
	if (confirm("Do you want to remove all UDP connections?\nGCS will have to re-connect.") === true) {
		let post_url = ROOT_URL + "api/settings/clients/clear_udp";
		const response = await fetch(post_url, {
			method: 'DELETE',
			headers: {
				'Accept': 'application/json',
				'Content-Type': 'application/json',
				"charset": 'UTF-8'
			},
			body: null
		});
		if (!response.ok) {
			conn_status = 0
			const message = `An error has occured: ${response.status}`;
			throw new Error(message);
		}
	} else {
		// cancel
	}
}

function show_toast(msg, background_color = "#0058a6") {
	Toastify({
		text: msg,
		duration: 5000,
		newWindow: true,
		close: true,
		gravity: "top", // `top` or `bottom`
		position: "center", // `left`, `center` or `right`
		style: {
			background: background_color,
			color: "#ff9734",
			borderColor: "#ff9734",
			borderStyle: "solid",
			borderRadius: "2px",
			borderWidth: "1px",
		},
		stopOnFocus: true, // Prevents dismissing of toast on hover
	}).showToast();
}

function check_validity() {
	let valid = true;
	let wifi_pass = document.getElementById("wifi_pass");
	let sta_pass  = document.getElementById("sta_pass");
	if (!wifi_pass.checkValidity()) {
		show_toast("Error: AP password must be 8-63 characters");
		valid = false;
	}
	if (document.getElementById("esp32_mode").value === "2" && !sta_pass.checkValidity()) {
		show_toast("Error: STA password must be 8-63 characters");
		valid = false;
	}
	return valid;
}

function check_for_issues() {
	let issue_div = document.getElementById("issue_div");
	if (set_telem_proto === "4" && serial_dec_mav_msgs === 0 && recv_ser_bytes !== 0) {
		issue_div.style.display = "block";
	} else {
		issue_div.style.display = "none";
	}
}

function save_settings() {
	let form = document.getElementById("settings_form")
	if (check_validity()) {
		let json_data = toJSONString(form)
		send_json("api/settings", json_data).then(send_response => {
			console.log(send_response);
			conn_status = 1
			show_toast(send_response["msg"])
			get_settings()  // update UI with new settings
		}).catch(error => {
			show_toast(error.message);
		});
	} else {
		console.log("Form was not filled out correctly.")
	}
}
