module;

#if defined(_WIN32)
#include <Windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#elif defined(__linux__)
#include <gtk/gtk.h>
#else
#error "Platform not supported!"
#endif

export module NMB;

export namespace NMB
{

    enum Result
    {
        CANCEL,
        OK
    };

    enum Icon
    {
        ICON_INFO,
        ICON_WARNING,
        ICON_ERROR
    };


    [[nodiscard]] Result show( const char* p_title, const char* p_message, Icon icon )
    {
#if defined(_WIN32)

        int icon_flag;

        switch( icon )
        {
            case NMB::Icon::ICON_INFO:
                icon_flag = MB_ICONINFORMATION;
                break;
            case NMB::Icon::ICON_WARNING:
                icon_flag = MB_ICONWARNING;
                break;
            case NMB::Icon::ICON_ERROR:
                icon_flag = MB_ICONERROR;
                break;
        }

        int result = MessageBoxA( nullptr, p_message, p_title, MB_OKCANCEL | MB_SYSTEMMODAL | icon_flag );

        if( result == IDOK )
            return NMB::Result::OK;
        else
            return NMB::Result::CANCEL;

#elif defined(__APPLE__)

        CFOptionFlags cf_alert_icon;

        switch( icon )
        {
            case NMB::Icon::ICON_INFO:
                cf_alert_icon = kCFUserNotificationNoteAlertLevel;
                break;
            case NMB::Icon::ICON_WARNING:
                cf_alert_icon = kCFUserNotificationCautionAlertLevel;
                break;
            case NMB::Icon::ICON_ERROR:
                cf_alert_icon = kCFUserNotificationStopAlertLevel;
                break;
        }

        CFStringRef cf_title   = CFStringCreateWithCString( kCFAllocatorDefault, p_title,   kCFStringEncodingUTF8 );
        CFStringRef cf_message = CFStringCreateWithCString( kCFAllocatorDefault, p_message, kCFStringEncodingUTF8 );

        CFOptionFlags result;

        CFUserNotificationDisplayAlert( 0, cf_alert_icon, nullptr, nullptr, nullptr, cf_title, cf_message, CFSTR("OK"), CFSTR("Cancel"), nullptr, &result );

        CFRelease( cf_title   );
        CFRelease( cf_message );

        if( result == kCFUserNotificationDefaultResponse )
            return NMB::Result::OK;
        else
            return NMB::Result::CANCEL;

#elif defined(__linux__)

        GtkMessageType gtk_message_type;

        switch( icon )
        {
            case NMB::Icon::ICON_INFO:
                gtk_message_type = GTK_MESSAGE_INFO;
                break;
            case NMB::Icon::ICON_WARNING:
                gtk_message_type = GTK_MESSAGE_WARNING;
                break;
            case NMB::Icon::ICON_ERROR:
                gtk_message_type = GTK_MESSAGE_ERROR;
                break;
        }

        GtkWidget* p_dialog = gtk_message_dialog_new( nullptr, GTK_DIALOG_DESTROY_WITH_PARENT, gtk_message_type, GTK_BUTTONS_OK_CANCEL, "%s\n\n%s", p_title, p_message );
        gint result = gtk_dialog_run( GTK_DIALOG(p_dialog) );
        gtk_widget_destroy( p_dialog );

        if( result == GTK_RESPONSE_OK )
            return NMB::Result::OK;
        else
            return NMB::Result::CANCEL;

#endif
    }

}