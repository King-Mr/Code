﻿
// WMCPDATASDlg.h: 头文件
//

#pragma once


// CWMCPDATASDlg 对话框
class CWMCPDATASDlg : public CDialogEx
{
// 构造
public:
	CWMCPDATASDlg(CWnd* pParent = nullptr);	// 标准构造函数

// 对话框数据
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_WMCPDATAS_DIALOG };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV 支持


// 实现
protected:
	HICON m_hIcon;

	// 生成的消息映射函数
	virtual BOOL OnInitDialog();
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()
public:
	afx_msg BOOL OnCopyData(CWnd* pWnd, COPYDATASTRUCT* pCopyDataStruct);
	CListBox m_strServerRecv;
	afx_msg void OnLbnSelchangeList1();
	afx_msg void OnBnClickedDelall();
};
