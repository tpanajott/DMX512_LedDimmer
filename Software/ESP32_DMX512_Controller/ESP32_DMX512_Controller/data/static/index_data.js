var hasLostConnection = false;
var doConnectionCheck = true;
var socket;

function connectWebSocket() {
    socket = new WebSocket(`ws://${window.location.host}/index_data`);

    socket.onmessage = function (event) {
        var json_data = JSON.parse(event.data);
        console.log(json_data);

        if ("channels" in json_data) {
            for (let i = 0; i < 4; i++) {
                if (json_data["channels"][i]["state"] == 1) {
                    $("#channel" + (i + 1) + "_output").html(json_data["channels"][i]["level"]);
                    $("#channel" + (i + 1) + "_output_slider").val(json_data["channels"][i]["level"]);
                } else {
                    $("#channel" + (i + 1) + "_output").html("Off");
                    $("#channel" + (i + 1) + "_output_slider").val(0);
                }
                // Only level and state is sent is sent during status updates. By checking for "enabled"
                // we can make sure this is the full status update send during first connection.
                if ("enabled" in json_data["channels"][i]) {
                    $("#channel" + (i + 1) + "_enabled").prop("checked", json_data["channels"][i]["enabled"]);
                    $("#channel" + (i + 1) + "_name").val(json_data["channels"][i]["name"]);
                    $("#channel" + (i + 1) + "_channel").val(json_data["channels"][i]["channel"]);
                    $("#channel" + (i + 1) + "_output_slider").data("channel", json_data["channels"][i]["channel"]);
                    $("#channel" + (i + 1) + "_min").val(json_data["channels"][i]["min"]);
                    $("#channel" + (i + 1) + "_max").val(json_data["channels"][i]["max"]);
                    $("#channel" + (i + 1) + "_dimmingSpeed").val(json_data["channels"][i]["dimmingSpeed"]);
                    $("#channel" + (i + 1) + "_autoDimmingSpeed").val(json_data["channels"][i]["autoDimmingSpeed"]);
                    $("#channel" + (i + 1) + "_holdPeriod").val(json_data["channels"][i]["holdPeriod"]);
                }
            }
        }
        if ("buttons" in json_data) {
            for (let i = 0; i < 4; i++) {
                $("#button" + (i + 1) + "_enabled").prop("checked", json_data["buttons"][i]["enabled"]);
                $("#button" + (i + 1) + "_channel").val(json_data["buttons"][i]["channel"]);
            }
        }

        if ("button_min_time" in json_data) {
            $("#button_min_press").val(json_data["button_min_time"]);
        }
        if ("button_max_time" in json_data) {
            $("#button_max_press").val(json_data["button_max_time"]);
        }

        // TODO: Cleanup and perform all population of data as above.
        $.each(json_data, function (index, value) {
            // Button values are not set with .val, they are set with .html
            // and by doing that, we cannot use the switch statement farther down
            if (index.startsWith("button") && index.endsWith("_output")) {
                $(`#${index}`).html(value);
                if (value == "DISABLED") {
                    $(`#${index}_slider`).prop("disabled", "true");
                } else {
                    if (value == "Off") {
                        $(`#${index}_slider`).val(0);
                    } else {
                        $(`#${index}_slider`).val(value);
                    }
                }
            } else if (index.startsWith("button") && index.endsWith("_name")) {
                $(`#${index}`).val(value);
                $(`#${index}_title`).html(value);
            } else if (index.startsWith("button") && index.endsWith("_enabled")) {
                $(`#${index}`).prop("checked", value);
            } else if (index == "mqtt_auth") {
                $(`#${index}`).prop("checked", value);
                handleMqttUsernamePasswordVisibilityState();
            } else if (index == "mqtt_status") {
                $(`#${index}`).html(value);
                if (value == "Connected") {
                    $("#mqtt_connection_error").addClass("hidden");
                    $("#mqtt_status").prop("class", "tag is-success");
                } else {
                    $("#mqtt_connection_error").removeClass("hidden");
                    $("#mqtt_status").prop("class", "tag is-danger");
                }
            } else if (index == "home_assistant_status") {
                $(`#${index}`).html(value);
                if (value == "Connected") {
                    $("#home_assistant_connection_error").addClass("hidden");
                    $("#home_assistant_status").prop("class", "tag is-success");
                } else {
                    $("#home_assistant_connection_error").removeClass("hidden");
                    $("#home_assistant_status").prop("class", "tag is-danger");
                }
            } else if (index == "log_level") {
                $("#log_level").val(value).change();
            } else {
                if ($(`#${index}`).length) {
                    $(`#${index}`).val(value);
                } else {
                    console.log(`[ERROR] Trying to set value for element that doesn't exists: #${index}`);
                }
            }
        });
    }

    socket.onerror = function (event) {
        console.log(`error:`);
        console.log(event);
    }

    socket.onclose = function () {
        setTimeout(function () {
            connectWebSocket();
        }, 1000);
    };
}

function sendLightUpdateFromSlider(slider) {
    var message = {
        "channel": $(slider).data('channel'),
        "value": parseInt(slider.value)
    };
    console.log(message);
    socket.send(JSON.stringify(message));
}

// Websocket timeout handler
function connectionMonitor() {
    // Do a simple HTTP GET request to check if the device is reachable
    if (doConnectionCheck) {
        $.ajax({
            url: `http://${window.location.host}/connection_test`,
            success: function (data) {
                $("#not_connected_modal").removeClass("is-active");
                if (hasLostConnection) {
                    location.reload();
                }
            },
            error: function (data, textStatus, errorThrown) {
                hasLostConnection = true;
                $("#not_connected_modal").addClass("is-active");
            },
            timeout: 1000 //in milliseconds
        });
    }
    setTimeout(connectionMonitor, 1000);
}

function handleMqttUsernamePasswordVisibilityState() {
    if ($("#mqtt_auth").prop("checked") == true) {
        $("#mqtt_username_field").show();
        $("#mqtt_psk_field").show();
    } else {
        $("#mqtt_username_field").hide();
        $("#mqtt_psk_field").hide();
    }
}

function setupWifiSelectModal() {
    $("#select_wifi_modal_close_background").click(function () {
        $("#select_wifi_modal").removeClass("is-active");
        doConnectionCheck = true;
    });

    $("#select_wifi_modal_close_button").click(function () {
        $("#select_wifi_modal").removeClass("is-active");
        doConnectionCheck = true;
    });

    $("#select_wifi_modal_cancel_button").click(function () {
        $("#select_wifi_modal").removeClass("is-active");
        doConnectionCheck = true;
    });
}

function showWifiSelectModal() {
    $("#available_wifi_networks").hide();
    $("#wifi_failed_to_get_networks").hide();
    $("#wifi_no_networks_found").hide();
    $("#wifi_wait_div").show();
    $("#select_wifi_modal").addClass("is-active");
    // setTimeout(function() {
    //     $("#wifi_wait_div").hide();
    //     $("#available_wifi_networks").show();
    // }, 1000);
    // The first scan will always return 0 results. If we get 0 results, wait 5 seconds and try again.
    doConnectionCheck = false;
    $.get("/available_wifi_networks", function (data) {
        if (data.length <= 0) {
            setTimeout(function () {
                $.get("/available_wifi_networks", function (data) {
                    if (data.length <= 0) {
                        $("#wifi_wait_div").hide();
                        $("#wifi_no_networks_found").show();
                    } else {
                        populateModalWithWiFiNetworks(data);
                    }
                }).fail(function () {
                    $("#wifi_wait_div").hide();
                    $("#wifi_failed_to_get_networks").show();
                });
            }, 5000)
        } else {
            populateModalWithWiFiNetworks(data);
        }
    }).fail(function () {
        $("#wifi_wait_div").hide();
        $("#wifi_failed_to_get_networks").show();
    });
}

function doFactoryReset() {
    $.get("/do_factory_reset", function () {
        window.location = "/reboot";
    }).fail(function () {
        alert("Failed to perform factory reset!");
    });
}

function showFactoryResetModal() {
    $("#factory_reset_modal").addClass("is-active");
}

function cancelFactoryReset() {
    $("#factory_reset_modal").removeClass("is-active");
}

function selectNetwork(network) {
    $("#wifi_ssid").val(network);
    $("#select_wifi_modal").removeClass("is-active");
    $("#wifi_psk").select(); // Select the PSK field to accept the PSK for the select WiFi
    doConnectionCheck = true;
}

function populateModalWithWiFiNetworks(networks) {
    var table_body_html = "";
    $.each(networks, function (index, network) {
        table_body_html += "<tr>";

        table_body_html += "<th>";
        table_body_html += `<a onclick="selectNetwork('${network.ssid}');">${network.ssid}</a>`;
        table_body_html += "</th>";

        table_body_html += "<td>";
        table_body_html += network.rssi;
        table_body_html += "</td>";

        table_body_html += "<td>";
        table_body_html += network.channel;
        table_body_html += "</td>";

        table_body_html += "<td>";
        table_body_html += network.security;
        table_body_html += "</td>";

        table_body_html += "</tr>";
    });
    $("#available_wifi_networks_tbody").html(table_body_html);
    $("#wifi_wait_div").hide();
    $("#available_wifi_networks").show();
}

// A $( document ).ready() block.
$(document).ready(function () {
    setupWifiSelectModal();
    $("#factory_reset_modal_background").click(cancelFactoryReset);
    handleMqttUsernamePasswordVisibilityState(); // Set default visibility state
    // Connect function so that correct visibility is active when checking and unchecking the checkbox
    $('#mqtt_auth').change(handleMqttUsernamePasswordVisibilityState);
    connectWebSocket();
    setTimeout(connectionMonitor, 1000);
});