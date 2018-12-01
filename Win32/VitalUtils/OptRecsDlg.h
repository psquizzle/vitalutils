#pragma once
#include "afxwin.h"
#include "EditEx.h"

class COptRecsDlg : public CDialogEx {
	DECLARE_DYNAMIC(COptRecsDlg)

public:
	COptRecsDlg(CWnd* pParent = NULL);   // standard constructor
	virtual ~COptRecsDlg();

	enum { IDD = IDD_OPT_RECS };

public:

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	afx_msg void OnEnKillfocusInterval();
	DECLARE_MESSAGE_MAP()
public:
	BOOL m_bAbstime = FALSE;
	BOOL m_bPrintHeader = TRUE;
	BOOL m_bRestricted = FALSE;
	BOOL m_bLast = FALSE;
	double m_fInterval = 60.0;
	CEditEx m_ctrInterval;

public:
	virtual void OnOK() {};
	virtual void OnCancel() {};
	virtual BOOL OnInitDialog();
	BOOL m_bPrintFilename;
	CEditEx m_ctrlOutputFile;
	BOOL m_bLong;
	afx_msg void OnBnClickedLong();
	CString m_strOutputFile = "output.csv";
	CButton m_ctrlHeader;
	CButton m_ctrlPrintFilename;
};
