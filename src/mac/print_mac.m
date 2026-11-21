/*
 * print_mac.m - a printed page on a Mac: the system's print dialog, with the
 * PDF print.c made as the document. The dialog is the Mac's own, so it offers
 * every printer, the paper, a preview, and "Save as PDF". The game ran on
 * this (the main) thread and waits here, as it would for a message box.
 */
#import <AppKit/AppKit.h>
#import <PDFKit/PDFKit.h>

int madeline_print_hand_over(const char *pdf, const char *title) {
    @autoreleasepool {
        PDFDocument *doc = [[PDFDocument alloc] initWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:pdf]]];
        if (!doc) return 0;
        /* The page is not any paper's size: it is fitted to the sheet chosen in the dialog, turned if that suits it better. */
        NSPrintInfo *info = [[NSPrintInfo sharedPrintInfo] copy];
        NSPrintOperation *op = [doc printOperationForPrintInfo:info scalingMode:kPDFPrintPageScaleToFit autoRotate:YES];
        if (!op) return 0;
        if (title && *title) op.jobTitle = [NSString stringWithUTF8String:title];   /* also the name "Save as PDF" suggests */
        op.showsPrintPanel = YES;
        op.showsProgressPanel = YES;
        [NSApp activateIgnoringOtherApps:YES];
        [op runOperation];                                 /* until Print or Cancel; either way the page was handed over */
        return 1;
    }
}
