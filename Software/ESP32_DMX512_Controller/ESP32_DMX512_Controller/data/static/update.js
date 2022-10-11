var hasLostConnection = false;
var doConnectionCheck = true;
var firmwareFileSize = 0;
var spiffsFileSize = 0;

// Websocket timeout handler
function connectionMonitor() { 
    // Do a simple HTTP GET request to check if the device is reachable
    if(doConnectionCheck) {
        $.ajax({
            url: `http://${window.location.host}/connection_test`,
            success: function(data){
                $("#not_connected_modal").removeClass("is-active");
                if(hasLostConnection) {
                    location.reload();
                }
            },
            error: function(data, textStatus, errorThrown) {
                hasLostConnection = true;
                $("#not_connected_modal").addClass("is-active");
            },
            timeout: 1000 //in milliseconds
         });
    }
    setTimeout(connectionMonitor, 1000);  
}

function performFirmwareUpload() {
    // Do a check to see if the file names are valid
    firmwareFileName = $('#firmware_file').prop('files')[0]["name"];
    spiffsFileName = $('#spiffs_file').prop('files')[0]["name"];

    if(!firmwareFileName.startsWith("firmware") || !firmwareFileName.endsWith(".bin")) {
        $("#update_error_text").html("Invalid firmware filename. Firmware file name must start with 'firmware' and end with '.bin'");
        $("#update_error").show();
        $("#update_progress").hide();
        $("#firmware_update_modal").addClass("is-active");
        return;
    } else if(!spiffsFileName.startsWith("littlefs") || !spiffsFileName.endsWith(".bin")) {
        $("#update_error_text").html("Invalid LittleFS filename. Firmware file name must start with 'littlefs' and end with '.bin'");
        $("#update_error").show();
        $("#update_progress").hide();
        $("#firmware_update_modal").addClass("is-active");
        return;
    }

    $("#update_progress").show();
    $("#update_error").hide();
    $("#firmware_update_modal").addClass("is-active");

    $.ajax({
        // Your server script to process the upload
        url: '/update',
        type: 'POST',
    
        // Form data
        data: new FormData($('form')[0]),
    
        // Tell jQuery not to process data or worry about content-type
        // You *must* include these options!
        cache: false,
        contentType: false,
        processData: false,
    
        // Custom XMLHttpRequest
        xhr: function () {
          var myXhr = $.ajaxSettings.xhr();
          if (myXhr.upload) {
            // For handling the progress of the upload
            myXhr.upload.addEventListener('progress', function (e) {
              if (e.lengthComputable) {
                var percent = Math.floor((e.loaded / e.total) * 100);
                $("#firmware_update_progress").prop("value", percent);
                $("#firmware_update_progress").html(`${percent}%`);
              }
            }, false);
          }
          return myXhr;
        },

        success: function() {
            // Update completed without errors. Do a reboot
            window.location = "/reboot";
        },
        error: function(data, textStatus, errorThrown) {
            $("#update_error_text").html(textStatus + ": " + errorThrown)
            $("#update_error").show();
            $("#update_progress").hide();
            console.log("Update failed");
            console.log(data);
        }
      });
}

// A $( document ).ready() block.
$(document).ready(function() {
    $("#firmware_file").change(function() {
        firmwareFileSize = $('#firmware_file').prop('files')[0]["size"];
        $("#firmware_file_name").html($('#firmware_file').prop('files')[0]["name"]);
    });

    $("#spiffs_file").change(function() {
        spiffsFileSize = $('#spiffs_file').prop('files')[0]["size"];
        $("#spiffs_file_name").html($('#spiffs_file').prop('files')[0]["name"]);
    });

    $("#firmware_spiffs_upload_form").submit(function(event) {
        event.preventDefault();
        performFirmwareUpload();
    });
});